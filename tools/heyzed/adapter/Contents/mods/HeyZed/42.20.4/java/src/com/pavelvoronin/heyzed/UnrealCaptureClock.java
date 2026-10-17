package com.pavelvoronin.heyzed;

final class UnrealCaptureClock {
    static long next(long deadline,long now,long period) {
        return deadline==0?now+period:deadline+(Math.max(0,now-deadline)/period+1)*period;
    }
    private UnrealCaptureClock(){}
}
