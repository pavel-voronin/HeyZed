package com.pavelvoronin.heyzed;

import java.time.*;
import org.joml.Vector3d;
import org.joml.Vector3f;

/** Low precision orbital elements and lunar perturbations (Paul Schlyter):
 * https://stjarnhimlen.se/comp/ppcomp.html . Independent Sun/Moon, no game-state writes.
 * Local axes: east +X, south +Y, up +Z. Kentucky civil time includes historical DST. */
final class UnrealCelestial {
    static final double LATITUDE=37.94,LONGITUDE=-85.96;
    static final ZoneId ZONE=ZoneId.of("America/Kentucky/Louisville");
    private static final double RAD=Math.PI/180,AU_EARTH_RADII=149597870.7/6378.137;
    record Equatorial(Vector3d sun,Vector3d moon,double sidereal,double elongation) {}
    record State(Vector3f sun,Vector3f moon,Vector3f moonToSun,float illuminated,boolean waning,float sunDiameter,float moonDiameter) {}
    static Instant instant(int year,int month,int day,float hour) {
        return LocalDate.of(year,month,day).atStartOfDay().plusNanos(Math.round(hour*3_600_000_000_000.0)).atZone(ZONE).toInstant();
    }
    static State at(int year,int month,int day,float hour) {return at(instant(year,month,day,hour),LATITUDE,LONGITUDE);}
    static State at(Instant time,double latitude,double longitude) {
        var eq=equatorial(time);double lst=eq.sidereal+longitude*RAD,lat=latitude*RAD;
        // Subtract the observer in equatorial coordinates for lunar parallax.
        double geolat=lat-.1924*RAD*Math.sin(2*lat),rho=.99833+.00167*Math.cos(2*lat);
        var observer=new Vector3d(Math.cos(geolat)*Math.cos(lst),Math.cos(geolat)*Math.sin(lst),Math.sin(geolat)).mul(rho);
        var sunTopo=new Vector3d(eq.sun).mul(AU_EARTH_RADII).sub(observer);
        var moonTopo=new Vector3d(eq.moon).sub(observer);
        var moonToSun=new Vector3d(eq.sun).mul(AU_EARTH_RADII).sub(eq.moon).normalize();
        float phase=(float)((1-moonToSun.dot(new Vector3d(moonTopo).normalize()))*.5);
        return new State(horizontal(sunTopo,lst,lat),horizontal(moonTopo,lst,lat),horizontal(moonToSun,lst,lat),
            Math.max(0,Math.min(1,phase)),eq.elongation>180,
            (float)(2*Math.asin(695700.0/(sunTopo.length()*6378.137))),
            (float)(2*Math.asin(1737.4/(moonTopo.length()*6378.137))));
    }
    static Equatorial equatorial(Instant time) {
        double days=(time.getEpochSecond()+time.getNano()*1e-9)/86400.0-10956;
        double sw=282.9404+4.70935e-5*days,se=.016709-1.151e-9*days,sm=356.0470+.9856002585*days;
        double eccentric=kepler(sm*RAD,se),sx=Math.cos(eccentric)-se,sy=Math.sqrt(1-se*se)*Math.sin(eccentric);
        double sunR=Math.hypot(sx,sy),sunLon=Math.atan2(sy,sx)/RAD+sw;
        double node=125.1228-.0529538083*days,inc=5.1454,mw=318.0634+.1643573223*days,mm=115.3654+13.0649929509*days;
        double e=kepler(mm*RAD,.0549),mx=60.2666*(Math.cos(e)-.0549),my=60.2666*Math.sqrt(1-.0549*.0549)*Math.sin(e);
        double mr=Math.hypot(mx,my),v=Math.atan2(my,mx)/RAD;
        double x=mr*(cos(node)*cos(v+mw)-sin(node)*sin(v+mw)*cos(inc));
        double y=mr*(sin(node)*cos(v+mw)+cos(node)*sin(v+mw)*cos(inc)),z=mr*sin(v+mw)*sin(inc);
        double lon=Math.atan2(y,x)/RAD,lat=Math.atan2(z,Math.hypot(x,y))/RAD;
        double ls=sm+sw,lm=mm+mw+node,d=lm-ls,f=lm-node;
        lon+=-1.274*sin(mm-2*d)+.658*sin(2*d)-.186*sin(sm)-.059*sin(2*mm-2*d)-.057*sin(mm-2*d+sm)
            +.053*sin(mm+2*d)+.046*sin(2*d-sm)+.041*sin(mm-sm)-.035*sin(d)-.031*sin(mm+sm)-.015*sin(2*f-2*d)+.011*sin(mm-4*d);
        lat+=-.173*sin(f-2*d)-.055*sin(mm-f-2*d)-.046*sin(mm+f-2*d)+.033*sin(f+2*d)+.017*sin(2*mm+f);
        mr+=-.58*cos(mm-2*d)-.46*cos(2*d);
        double tilt=(23.4393-3.563e-7*days)*RAD;
        var sun=equatorialVector(sunR*cos(sunLon),sunR*sin(sunLon),0,tilt);
        var moon=equatorialVector(mr*cos(lon)*cos(lat),mr*sin(lon)*cos(lat),mr*sin(lat),tilt);
        double utcHours=(days-Math.floor(days))*24;
        return new Equatorial(sun,moon,(ls+180+utcHours*15)*RAD,wrap(lon-sunLon));
    }
    private static Vector3d equatorialVector(double x,double y,double z,double tilt){return new Vector3d(x,y*Math.cos(tilt)-z*Math.sin(tilt),y*Math.sin(tilt)+z*Math.cos(tilt));}
    private static Vector3f horizontal(Vector3d p,double lst,double lat) {
        double east=-Math.sin(lst)*p.x+Math.cos(lst)*p.y;
        double meridian=Math.cos(lst)*p.x+Math.sin(lst)*p.y;
        return new Vector3f((float)east,(float)(Math.sin(lat)*meridian-Math.cos(lat)*p.z),(float)(Math.cos(lat)*meridian+Math.sin(lat)*p.z)).normalize();
    }
    private static double kepler(double anomaly,double eccentricity){
        double m=Math.IEEEremainder(anomaly,Math.PI*2),e=m;
        for(int i=0;i<6;i++)e-=(e-eccentricity*Math.sin(e)-m)/(1-eccentricity*Math.cos(e));return e;
    }
    private static double sin(double x){return Math.sin(x*RAD);}
    private static double cos(double x){return Math.cos(x*RAD);}
    private static double wrap(double x){return (x%360+360)%360;}
    private UnrealCelestial(){}
}
