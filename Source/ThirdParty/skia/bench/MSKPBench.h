/*
 * Copyright 2021 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef MSKPBench_DEFINED
#define MSKPBench_DEFINED

#include "bench/Benchmark.h"

class MSKPPlayer;

class MSKPBench : public Benchmark {
public:
    MSKPBench(SkString name, std::unique_ptr<MSKPPlayer> player);
    ~MSKPBench() override;

protected:
    void onDraw(int loops, SkCanvas*) override { SkASSERT(false); }
    void onDrawFrame(int loops, SkCanvas*, std::function<void()> submitFrame) override;
    const char* onGetName() override;
    SkISize onGetSize() override;
    void onPreDraw(SkCanvas*) override;
    void onPostDraw(SkCanvas*) override;

private:
    bool submitsInternalFrames() override { return true; }

    SkString fName;
    std::unique_ptr<MSKPPlayer> fPlayer;
};

#endif
