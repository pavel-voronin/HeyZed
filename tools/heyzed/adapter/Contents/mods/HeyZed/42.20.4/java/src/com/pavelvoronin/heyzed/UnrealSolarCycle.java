package com.pavelvoronin.heyzed;

import java.time.LocalDate;
import org.joml.Vector3f;
import zombie.erosion.season.ErosionSeason;

/** Reparameterizes only the solar hour angle. The calendar and other bodies never use this clock. */
final class UnrealSolarCycle {
    private static final double TAU=2*Math.PI,LAT=Math.toRadians(UnrealCelestial.LATITUDE);
    // Dense near the endpoints where the inverse exposure curve changes most quickly.
    private static final double[] KNOTS=knots();
    private static final double[] HEIGHTS=heights();
    record Day(double dawn,double dusk) {
        double twilight(){return (24-dusk+dawn)/4;}
        double morning(){return dawn-twilight();}
        double evening(){return dusk+twilight();}
    }
    record Schedule(Day previous,Day today,Day next) {}
    private static ErosionSeason cachedSeason;
    private static LocalDate cachedDate;
    private static Schedule cached;


    static Schedule schedule(ErosionSeason season,LocalDate date) {
        var today=new Day(season.getDawn(),season.getDusk());
        if(season!=cachedSeason||!date.equals(cachedDate)||cached==null||!today.equals(cached.today)) {
            var copy=season.clone();
            var previous=read(copy,date.minusDays(1));var next=read(copy,date.plusDays(1));
            cached=new Schedule(previous,today,next);cachedSeason=season;cachedDate=date;
        }
        return cached;
    }
    private static Day read(ErosionSeason season,LocalDate date){season.setDay(date.getDayOfMonth(),date.getMonthValue()-1,date.getYear());return new Day(season.getDawn(),season.getDusk());}

    static UnrealCelestial.State at(UnrealCelestial.State real,double hour,Schedule schedule,boolean endlessDay,boolean endlessNight) {
        double sinDec=real.sun().z*Math.sin(LAT)-real.sun().y*Math.cos(LAT);
        double dec=Math.asin(Math.clamp(sinDec,-1,1));
        double angle=endlessNight?Math.PI:endlessDay?0:angle(hour,schedule,dec);
        double c=Math.cos(dec),s=Math.sin(dec),meridian=c*Math.cos(angle);
        var sun=new Vector3f((float)(-c*Math.sin(angle)),(float)(Math.sin(LAT)*meridian-Math.cos(LAT)*s),
            (float)(Math.cos(LAT)*meridian+Math.sin(LAT)*s)).normalize();
        return new UnrealCelestial.State(sun,real.moon(),real.moonToSun(),real.illuminated(),real.waning(),real.sunDiameter(),real.moonDiameter());
    }

    static double angle(double hour,Schedule schedule,double declination) {
        var day=schedule.today;double duration=day.dusk-day.dawn;
        if(!Double.isFinite(duration)||duration<2||duration>22)return (hour-12)*TAU/24;
        double hDay=hourAngle(.26,declination),hNight=hourAngle(HEIGHTS[0],declination);
        double twilight=day.twilight();
        // Inverse quadratic endpoints give a finite incoming twilight speed.
        double lastSecant=(hourAngle(HEIGHTS[HEIGHTS.length-2],declination)-hDay)/(1-KNOTS[KNOTS.length-2]);
        double edge=Math.min(lastSecant/twilight,1.8*(2*hDay/duration));
        double prevEnd=schedule.previous.evening()-24,nextStart=schedule.next.morning()+24;
        double morningSpeed=(TAU-2*hNight)/(day.morning()-prevEnd);
        double eveningSpeed=(TAU-2*hNight)/(nextStart-day.evening());
        // Around midsummer vanilla evening twilight continues after midnight.
        // Finish yesterday's curve before entering the shared overnight segment.
        if(hour<prevEnd) {
            var previous=schedule.previous;double previousTwilight=previous.twilight();
            double previousEdge=Math.min(lastSecant/previousTwilight,1.8*(2*hDay/(previous.dusk-previous.dawn)));
            return -twilight(1-(hour+24-previous.dusk)/previousTwilight,declination,morningSpeed*previousTwilight,previousEdge*previousTwilight)-TAU;
        }
        if(hour<day.morning())return hNight-TAU+travel(hour-prevEnd,day.morning()-prevEnd,TAU-2*hNight,morningSpeed);
        if(hour<day.dawn)return twilight((hour-day.morning())/twilight,declination,morningSpeed*twilight,edge*twilight);
        if(hour<=day.dusk)return -hDay+travel(hour-day.dawn,duration,2*hDay,edge);
        if(hour<day.evening())return -twilight(1-(hour-day.dusk)/twilight,declination,eveningSpeed*twilight,edge*twilight);
        return hNight+travel(hour-day.evening(),nextStart-day.evening(),TAU-2*hNight,eveningSpeed);
    }
    // Continuous speed and acceleration; all compensating speed is gained/lost around noon.
    private static double travel(double t,double duration,double distance,double edgeSpeed) {
        double u=t/duration;
        return edgeSpeed*t+(distance-edgeSpeed*duration)*(u-Math.sin(TAU*u)/TAU);
    }
    private static double hourAngle(double height,double dec){return Math.acos(Math.clamp((height-Math.sin(LAT)*Math.sin(dec))/(Math.cos(LAT)*Math.cos(dec)),-1,1));}
    private static double value(int i,double dec){return -hourAngle(HEIGHTS[i],dec);}
    private static double slope(int i,double dec,double start,double end) {
        if(i==0)return start;if(i==KNOTS.length-1)return end;
        double left=KNOTS[i]-KNOTS[i-1],right=KNOTS[i+1]-KNOTS[i];
        double a=(value(i,dec)-value(i-1,dec))/left,b=(value(i+1,dec)-value(i,dec))/right;
        if(a<=0||b<=0)return 0;
        // Monotone weighted harmonic derivative: no reversal or overshoot between exposure samples.
        return 3*(left+right)/((2*right+left)/a+(right+2*left)/b);
    }
    private static double twilight(double u,double dec,double start,double end) {
        int i=0;while(i<KNOTS.length-2&&u>KNOTS[i+1])i++;
        double width=KNOTS[i+1]-KNOTS[i],x=(u-KNOTS[i])/width,x2=x*x,x3=x2*x;
        return (2*x3-3*x2+1)*value(i,dec)+(x3-2*x2+x)*width*slope(i,dec,start,end)
            +(-2*x3+3*x2)*value(i+1,dec)+(x3-x2)*width*slope(i+1,dec,start,end);
    }
    private static double[] heights() {
        var heights=new double[KNOTS.length];
        heights[0]=Math.sin(Math.toRadians(-18));heights[heights.length-1]=.26;
        for(int i=1;i<heights.length-1;i++) {
            double target=Math.sin(KNOTS[i]*Math.PI/2),lo=heights[0],hi=.26;
            for(int n=0;n<48;n++){double mid=(lo+hi)*.5;if(daylight((float)mid)<target)lo=mid;else hi=mid;}
            heights[i]=(lo+hi)*.5;
        }
        return heights;
    }
    private static double[] knots(){var knots=new double[103];knots[1]=.002;knots[2]=.005;for(int i=3;i<knots.length;i++)knots[i]=(i-2)/100.0;return knots;}
    private static float daylight(float height) {
        if(height>=0)return .23f+.77f*smooth(0,.26f,height);
        float degrees=(float)Math.toDegrees(Math.asin(Math.max(-1,height)));
        return .23f*(float)Math.pow(10,degrees/4)*smooth(-18,-12,degrees);
    }
    private static float smooth(float a,float b,float value){float t=Math.clamp((value-a)/(b-a),0,1);return t*t*(3-2*t);}
    private UnrealSolarCycle(){}
}
