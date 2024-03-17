/*
 * BootCnt.h
 */
#pragma once

#include <sys/types.h>

class BootCnt
{
public:
    static BootCnt & Instance();

    BootCnt() = default;

    void Init();         // increment the counter and save it
    void Check() const;  // check and restore in case page erased

    u_long Cnt() const { return mCnt; };
private:
    u_long mCnt{0};      // boot counter
};
