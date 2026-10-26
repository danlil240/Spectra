#pragma once

// RAII helper that sets / unsets an environment variable for the duration of
// a test and restores the previous value afterwards.  Portable (POSIX + MSVC).

#include <cstdlib>
#include <optional>
#include <string>

namespace spectra::test
{

inline void set_env(const char* name, const char* value)
{
#ifdef _WIN32
    ::_putenv_s(name, value);
#else
    ::setenv(name, value, 1);
#endif
}

inline void unset_env(const char* name)
{
#ifdef _WIN32
    ::_putenv_s(name, "");
#else
    ::unsetenv(name);
#endif
}

class EnvGuard
{
   public:
    explicit EnvGuard(const char* name) : name_(name)
    {
        if (const char* v = std::getenv(name))
            old_ = std::string(v);
    }
    ~EnvGuard()
    {
        if (old_)
            set_env(name_.c_str(), old_->c_str());
        else
            unset_env(name_.c_str());
    }
    EnvGuard(const EnvGuard&)            = delete;
    EnvGuard& operator=(const EnvGuard&) = delete;

    void set(const std::string& value) { set_env(name_.c_str(), value.c_str()); }
    void unset() { unset_env(name_.c_str()); }

   private:
    std::string                name_;
    std::optional<std::string> old_;
};

}   // namespace spectra::test
