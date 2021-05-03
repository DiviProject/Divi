#ifndef I_CLOCK_H
#define I_CLOCK_H
#include <stdint.h>
class I_Clock
{
public:
    virtual int64_t getTime() const = 0;
};
#endif // I_CLOCK_H