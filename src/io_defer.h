#include <functional>

class Defer {
public:
    Defer(const std::function<void()>& func) : _func(func) {}

    ~Defer() {
        _func();
    }
    
private:
    std::function<void()> _func;
};

