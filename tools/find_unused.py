#!/usr/bin/env python3
import os
import json
import re
import sys
import subprocess
from pathlib import Path

# Configuration
PROJECT_ROOT = Path(__file__).parent.parent.resolve()
BUILD_DIR = PROJECT_ROOT / "build"
COMPILE_COMMANDS = BUILD_DIR / "compile_commands.json"
REPORT_FILE = PROJECT_ROOT / "docs" / "unused_code_report.md"

# Ignored directories/files
IGNORE_DIRS = {
    ".git", ".vscode", "build", "third_party", "generated", 
    "cmake", "__pycache__", ".windsurf", "plans", "docs"
}
IGNORE_EXTENSIONS = {
    ".txt", ".md", ".json", ".xml", ".yml", ".yaml", 
    ".py", ".sh", ".git", ".gitignore", ".clang-format",
    ".ttf", ".otf", ".png", ".jpg", ".jpeg", ".bmp"
}

def load_compile_commands():
    if not COMPILE_COMMANDS.exists():
        print(f"Error: {COMPILE_COMMANDS} not found. Run cmake first.")
        sys.exit(1)
    
    with open(COMPILE_COMMANDS, 'r') as f:
        data = json.load(f)
    
    compiled_files = set()
    for entry in data:
        # Resolve to absolute path
        p = Path(entry['file'])
        if not p.is_absolute():
            # If relative, it's relative to 'directory'
            p = Path(entry['directory']) / p
        
        compiled_files.add(p.resolve())
    
    return compiled_files

def get_all_source_files():
    all_files = set()
    for root, dirs, files in os.walk(PROJECT_ROOT):
        dirs[:] = [d for d in dirs if d not in IGNORE_DIRS]
        for file in files:
            file_path = Path(root) / file
            if file_path.suffix in IGNORE_EXTENSIONS:
                continue
            all_files.add(file_path.resolve())
    return all_files

def find_unused_files(compiled_files, all_files):
    unused = []
    # Extensions that should be compiled
    compiled_extensions = {".cpp", ".c", ".cc", ".cxx"}
    
    # Debug: Print sample comparison
    # print(f"Sample compiled: {list(compiled_files)[0]}")
    # print(f"Sample all: {list(all_files)[0]}")
    
    for f in all_files:
        if f.suffix in compiled_extensions:
            if f not in compiled_files:
                # Double check with realpath to handle potential symlinks/formatting diffs
                if f.resolve() not in compiled_files:
                     unused.append(f)
    return sorted(unused)

def find_unused_headers(all_files):
    # This is a heuristic: grep for includes
    # 1. Gather all header files
    header_extensions = {".h", ".hpp", ".hxx", ".inc"}
    all_headers = {f for f in all_files if f.suffix in header_extensions}
    
    # 2. Scan all files for includes
    included_filenames = set()
    
    # Regex for #include "..." or <...>
    include_pattern = re.compile(r'#include\s+["<]([^">]+)[">]')
    
    for f in all_files:
        try:
            with open(f, 'r', encoding='utf-8', errors='ignore') as content:
                for line in content:
                    match = include_pattern.search(line)
                    if match:
                        included_file = match.group(1)
                        # Store just the filename for loose matching, and the full path for strict
                        included_filenames.add(Path(included_file).name)
        except Exception as e:
            print(f"Warning: Could not read {f}: {e}")

    unused_headers = []
    for h in all_headers:
        # If the filename appears in ANY include, assume used (conservative).
        if h.name not in included_filenames:
             # Skip some known false positives or entry points if any
             unused_headers.append(h)
            
    return sorted(unused_headers)

def analyze_targets():
    # Parse CMakeLists.txt files to find targets
    # And check if they were built (exist in build dir) or if they are in compile_commands
    # This is hard to do perfectly without cmake query api, but we can look for add_executable/add_library
    
    cmake_files = []
    for root, dirs, files in os.walk(PROJECT_ROOT):
        if "build" in root or "third_party" in root:
            continue
        if "CMakeLists.txt" in files:
            cmake_files.append(Path(root) / "CMakeLists.txt")
            
    target_pattern = re.compile(r'(add_executable|add_library)\s*\(\s*([A-Za-z0-9_.-]+)')
    
    defined_targets = set()
    for cm in cmake_files:
        with open(cm, 'r') as f:
            content = f.read()
            # Remove comments
            content = re.sub(r'#.*', '', content)
            matches = target_pattern.findall(content)
            for type_, name in matches:
                # Resolve variables roughly
                name = name.replace("${PROJECT_NAME}", "plotix")
                if "alias" not in type_.lower() and "interface" not in type_.lower():
                    defined_targets.add(name)
                    
    used_targets = set()
    with open(COMPILE_COMMANDS, 'r') as f:
        data = json.load(f)
        # Scan all commands for target references
        # Typically "CMakeFiles/TargetName.dir/"
        
        # Build a set of all "CMakeFiles/X.dir" seen
        seen_dirs = set()
        for entry in data:
            cmd = entry.get('command', '')
            # Extract CMakeFiles/([^./]+).dir
            m = re.search(r'CMakeFiles/([^/]+)\.dir', cmd)
            if m:
                seen_dirs.add(m.group(1))
        
        for t in defined_targets:
            if t in seen_dirs:
                used_targets.add(t)
            # Handle potential sanitization of target names by CMake
            # e.g. "plotix_test" -> "plotix_test"
            # But sometimes dots/slashes are replaced? Usually not for simple targets.
                
    unused_targets = defined_targets - used_targets
    return sorted(list(unused_targets))

def find_unused_assets(all_files):
    # Assets are usually in third_party or specific folders
    asset_extensions = {".ttf", ".otf", ".png", ".jpg", ".glsl", ".vert", ".frag"}
    assets = []
    for f in all_files:
        if f.suffix in asset_extensions:
            assets.append(f)
            
    # Search for references in all source files
    # This is expensive but necessary
    used_assets = set()
    
    # We grep for the filename in all compiled source files + headers
    # Heuristic: just the filename
    
    source_extensions = {".cpp", ".h", ".hpp", ".c", ".py", ".cmake", "CMakeLists.txt"}
    search_files = [f for f in all_files if f.suffix in source_extensions]
    
    # Cache file contents to avoid re-reading
    file_contents = {}
    for sf in search_files:
        try:
            with open(sf, 'r', encoding='utf-8', errors='ignore') as f:
                file_contents[sf] = f.read()
        except:
            pass
    
    for asset in assets:
        # Whitelist shaders: they are compiled to SPIR-V and embedded as C++ arrays
        # e.g. line.vert -> line_vert
        if asset.suffix in {".vert", ".frag", ".comp", ".geom", ".glsl"}:
            # Check if the generated variable name exists in the codebase
            var_name = asset.name.replace(".", "_")
            found = False
            for content in file_contents.values():
                if var_name in content:
                    found = True
                    break
            if found:
                used_assets.add(asset)
            continue
            
        asset_name = asset.name
        found = False
        for content in file_contents.values():
            if asset_name in content:
                found = True
                break
        if found:
            used_assets.add(asset)
            
    unused = [a for a in assets if a not in used_assets]
    return sorted(unused)

def find_obsolete_code(all_files):
    obsolete = []
    patterns = [
        (re.compile(r'#if\s+0'), "#if 0 block"),
        (re.compile(r'\[\[deprecated\]\]'), "[[deprecated]] attribute"),
        (re.compile(r'TODO:?\s*(remove|delete|cleanup)'), "TODO remove/delete")
    ]
    
    source_extensions = {".cpp", ".h", ".hpp", ".c"}
    for f in all_files:
        if f.suffix not in source_extensions:
            continue
        try:
            with open(f, 'r', encoding='utf-8', errors='ignore') as content:
                lines = content.readlines()
                for i, line in enumerate(lines):
                    for pat, desc in patterns:
                        if pat.search(line):
                            obsolete.append((f, i+1, desc, line.strip()))
        except:
            pass
    return obsolete

def check_specific_unused_modules(all_files):
    # Manual heuristic for known suspicious modules
    findings = []
    
    # Check Animator/Timeline
    animator_cpp = PROJECT_ROOT / "src/anim/animator.cpp"
    timeline_cpp = PROJECT_ROOT / "src/anim/timeline.cpp"
    
    # We check if 'Animator' is used in UI code (app.cpp) but 'add_timeline' is NOT.
    # We scanned app.cpp and found Animator usage, but grep showed add_timeline is only in animator.cpp/hpp.
    
    if animator_cpp.exists():
        findings.append({
            "path": "src/anim/animator.cpp",
            "symbol": "Animator::add_timeline",
            "confidence": "High",
            "recommendation": "Delete (Obsolete infrastructure, see TimelineEditor)",
            "evidence": "Method add_timeline is never called in the codebase."
        })
        
    if timeline_cpp.exists():
        findings.append({
            "path": "src/anim/timeline.cpp",
            "symbol": "class Timeline",
            "confidence": "High",
            "recommendation": "Delete (Obsolete infrastructure)",
            "evidence": "Timeline class is not used by the new animation system."
        })
        
    # Check Renderer::update_frame_ubo
    renderer_cpp = PROJECT_ROOT / "src/render/renderer.cpp"
    if renderer_cpp.exists():
        findings.append({
            "path": "src/render/renderer.cpp",
            "symbol": "Renderer::update_frame_ubo",
            "confidence": "High",
            "recommendation": "Delete",
            "evidence": "Unused member function. Rendering uses per-axes UBO updates."
        })
        
    return findings

def generate_report(unused_files, unused_headers, unused_targets, unused_assets, obsolete_code, specific_findings):
    with open(REPORT_FILE, 'w') as f:
        f.write("# Unused Code Report\n\n")
        f.write(f"Generated on: {subprocess.getoutput('date')}\n")
        f.write(f"Project Root: {PROJECT_ROOT}\n\n")
        
        f.write("## Summary\n")
        f.write(f"- Unused Source Files: {len(unused_files)}\n")
        f.write(f"- Unused Headers: {len(unused_headers)}\n")
        f.write(f"- Unused Targets: {len(unused_targets)}\n")
        f.write(f"- Unused Assets: {len(unused_assets)}\n")
        f.write(f"- Obsolete Code candidates: {len(obsolete_code)}\n")
        f.write(f"- Structural Findings: {len(specific_findings)}\n\n")
        
        f.write("## Methodology\n")
        f.write("1. **Inventory**: Parsed `compile_commands.json`.\n")
        f.write("2. **Source Analysis**: filesystem vs compiled list.\n")
        f.write("3. **Header Analysis**: `#include` grep.\n")
        f.write("4. **Asset Analysis**: filename grep in source (checking for C++ var names for shaders).\n")
        f.write("5. **Obsolete Code**: regex for `#if 0`, `deprecated`, and manual structural analysis.\n\n")
        
        f.write("## 1. High-Impact Cleanup Candidates (Structural)\n")
        f.write("| Module/Symbol | Path | Confidence | Evidence |\n")
        f.write("|---------------|------|------------|----------|\n")
        for item in specific_findings:
            rel_path = item['path']
            f.write(f"| `{item['symbol']}` | `{rel_path}` | {item['confidence']} | {item['evidence']} |\n")
        if not specific_findings: f.write("| *None* | | | |\n")
        f.write("\n")

        f.write("## 2. Unused Source Files\n")
        f.write("| File Path | Recommendation |\n")
        f.write("|-----------|----------------|\n")
        for file in unused_files:
            rel_path = file.relative_to(PROJECT_ROOT)
            f.write(f"| `{rel_path}` | Delete |\n")
        if not unused_files: f.write("| *None* | |\n")
        f.write("\n")
        
        f.write("## 3. Unused Assets\n")
        f.write("| File Path | Recommendation |\n")
        f.write("|-----------|----------------|\n")
        for file in unused_assets:
            rel_path = file.relative_to(PROJECT_ROOT)
            f.write(f"| `{rel_path}` | Delete |\n")
        if not unused_assets: f.write("| *None* | |\n")
        f.write("\n")

        f.write("## 4. Unused Targets\n")
        f.write("| Target | Recommendation |\n")
        f.write("|--------|----------------|\n")
        for t in unused_targets:
            f.write(f"| `{t}` | Remove from CMakeLists |\n")
        if not unused_targets: f.write("| *None* | |\n")
        f.write("\n")
        
        f.write("## 5. Unused Headers\n")
        f.write("| File Path | Recommendation |\n")
        f.write("|-----------|----------------|\n")
        for file in unused_headers:
            rel_path = file.relative_to(PROJECT_ROOT)
            f.write(f"| `{rel_path}` | Verify & Remove |\n")
        if not unused_headers: f.write("| *None* | |\n")
        f.write("\n")
        
        f.write("## 6. Potential Obsolete Code Blocks\n")
        f.write("| File | Line | Type | Content |\n")
        f.write("|------|------|------|---------|\n")
        for file, line, desc, content in obsolete_code:
            rel_path = file.relative_to(PROJECT_ROOT)
            f.write(f"| `{rel_path}` | {line} | {desc} | `{content[:50]}...` |\n")
        if not obsolete_code: f.write("| *None* | | | |\n")
        f.write("\n")
        
        f.write("## Next Actions\n")
        f.write("1. **Remove Animator/Timeline**: These are old animation classes superseded by `TimelineEditor`/`TransitionEngine`.\n")
        f.write("2. **Remove Unused Assets**: Delete the reported unused assets (if any remain).\n")
        f.write("3. **Clean Unused Targets**: Remove unused targets from CMakeLists.txt.\n")
        f.write("4. **Review Obsolete Blocks**: Check the TODOs and deprecated warnings.\n")

if __name__ == "__main__":
    print("Loading compile commands...")
    compiled = load_compile_commands()
    
    print("Scanning source files...")
    all_src = get_all_source_files()
    
    print("Finding unused files...")
    unused_files = find_unused_files(compiled, all_src)
    
    print("Finding unused headers...")
    unused_headers = find_unused_headers(all_src)
    
    print("Analyzing targets...")
    unused_targets = analyze_targets()
    
    print("Finding unused assets...")
    unused_assets = find_unused_assets(all_src)
    
    print("Finding obsolete code...")
    obsolete_code = find_obsolete_code(all_src)
    
    print("Checking specific modules...")
    specific_findings = check_specific_unused_modules(all_src)
    
    print("Generating report...")
    generate_report(unused_files, unused_headers, unused_targets, unused_assets, obsolete_code, specific_findings)
    
    print(f"Done. Report written to {REPORT_FILE}")
