#define __warn_unused_result__ __far__

#include <cctype>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <math.h>
#include "ocropus.h"
#include "glinerec.h"
#include "glfmaps.h"
#include "ocr-utils.h"

using namespace iulib;
using namespace colib;
using namespace ocropus;
using namespace narray_ops;
using namespace narray_io;
using namespace glinerec;

namespace {
    float H(float x) {
        return x<0?0:x;
    }
    double uniform(double lo,double hi) {
        return drand48()*(hi-lo)+lo;
    }
    double loguniform(double lo,double hi) {
        return exp(drand48()*(log(hi)-log(lo))+log(lo));
    }

    inline float normorient(float x) {
        while(x>=M_PI/2) x -= M_PI;
        while(x<-M_PI/2) x += M_PI;
        return x;
    }
    inline float normadiff(float x) {
        while(x>=M_PI) x -= 2*M_PI;
        while(x<-M_PI) x += 2*M_PI;
        return x;
    }
    inline float normorientplus(float x) {
        while(x>=M_PI) x -= M_PI;
        while(x<0) x += M_PI;
        return x;
    }

    inline void checknan(floatarray &v) {
        int n = v.length1d();
        for(int i=0;i<n;i++)
            CHECK(!isnan(v.unsafe_at1d(i)));
    }

    inline float floordiv(float x,float y) {
        float n = floor(x/y);
        return x - n*y;
    }

    template <class T>
    inline void push_array(floatarray &out,narray<T> &in) {
        for(int i=0;i<in.length();i++)
            out.push() = in[i];
    }

    inline double sigmoid(double x) {
        if(x<-10) return 0;
        if(x>10) return 1;
        return 1/(1+exp(-x));
    }
    inline double ssigmoid(double x) {
        if(x<-10) return -1;
        if(x>10) return 1;
        return -1+2/(1+exp(-x));
    }
    void ssigmoid(floatarray &dt) {
        for(int i=0;i<dt.length();i++)
            dt[i] = ssigmoid(dt[i]);
    }
    void heaviside(floatarray &dt) {
        for(int i=0;i<dt.length();i++)
            if(dt[i]<0) dt[i] = 0;
    }
    void negheaviside(floatarray &dt) {
        for(int i=0;i<dt.length();i++) {
            if(dt[i]>0) dt[i] = 0;
            else dt[i] = -dt[i];
        }
    }
    float absfractile_nz(floatarray &dt,float f=0.9) {
        floatarray temp;
        for(int i=0;i<dt.length();i++) {
            float value = fabs(dt[i]);
            if(value<1e-6) continue;
            temp.push() = value;
        }
        return fractile(temp,f);
    }
    float absmean_nz(floatarray &dt) {
        float temp;
        int count;
        for(int i=0;i<dt.length();i++) {
            float value = fabs(dt[i]);
            if(value>1e-6) {
                temp += value;
                count++;
            }
        }
        return temp/count;
    }
    void apow(floatarray &dt,float p) {
        for(int i=0;i<dt.length();i++) {
            float value = dt[i];
            value = (value<0?-1:1) * pow(fabs(value),p);
            dt[i] = value;
        }
    }
}

namespace glinerec {
    struct SimpleFeatureMap : IFeatureMap {
        bytearray line;
        bytearray binarized;
        bytearray holes;
        bytearray junctions;
        bytearray endpoints;
        narray<floatarray> maps;
        floatarray troughs;
        floatarray dt;
        floatarray dt_x,dt_y;
        narray<floatarray> dt_maps;
        int pad;

        SimpleFeatureMap() {
            // parameters affecting all features
            pdef("ftypes","bejh","which feature types to extract (bgxyhejrt)");
            pdef("csize",40,"target character size after rescaling");
            pdef("context",1.3,"how much context to include");
            pdef("scontext",0.3,"value to multiply context pixels with (e.g., -1, 0, 1, 0.5)");
            pdef("aa",0.5,"amount of anti aliasing (-1 = use other algorithm)");
            pdef("maxheight",300,"maximum height for feature extraction");

            // parameters specific to individual feature maps
            pdef("skel_pre_smooth",0.0,"smooth by this amount prior to skeletal extraction");
            pdef("skel_post_dilate",3.0,"how much to smooth after skeletal extraction");
            pdef("grad_pre_smooth",2.0,"how much to smooth before gradient extraction");
            pdef("grad_post_smooth",0.0,"how much to smooth after gradient extraction");
            pdef("ridge_pre_smooth",1.0,"how much to smooth before skeletal extraction (about 0.5 to 3.0)");
            pdef("ridge_post_smooth",1.0,"how much to smooth after skeletal extraction (about 0.5 to 3.0)");
            pdef("ridge_asigma",0.6,"angle orientation bin overlap (about 0.5 to 1.5)");
            pdef("ridge_mpower",0.5,"gamma for ridge orientation map (about 0.2-2.0)");
            pdef("ridge_nmaps",4,"number of feature maps for ridge extraction (should be 4)");
            pdef("dt_power",1.0,"power to which to raise the distance transform");
            pdef("dt_asigma",0.7,"power to which to raise the distance transform");
            pdef("dt_which","inside","inside, outside, or both");
            pdef("dt_grad_smooth",1.0,"smoothing of the distance transform before gradient computation");
            pad = 10;
        }

        const char *name() {
            return "sfmap";
        }

        void save(FILE *stream) {
            magic_write(stream,"sfmap");
            psave(stream);
        }

        void load(FILE *stream) {
            magic_read(stream,"sfmap");
            pload(stream);
            reimport();
        }

        virtual void setLine(bytearray &image_) {
            const char *ftypes = pget("ftypes");
            maps.resize(int(pgetf("ridge_nmaps")));
            line = image_;
            dsection("setline");
            dclear(0);
            dshow(line,"yyy");
            pad_by(line,pad,pad,max(line));

            // compute a simple binarized version and segment
            binarize_simple(binarized,line);
            sub(255,binarized);
            remove_small_components(binarized,3,3);
            dshow(binarized,"yyy");

            // skeletal features
            if(strchr(ftypes,'j') || strchr(ftypes,'e')) {
                float presmooth = pgetf("skel_pre_smooth");
                int skelsmooth = pgetf("skel_post_dilate");
                ocropus::skeletal_features(endpoints,junctions,binarized,presmooth,skelsmooth);
                dshow(junctions,"yYY");
                dshow(endpoints,"Yyy");
            }

            // computing holes
            if(strchr(ftypes,'h')) {
                extract_holes(holes,binarized);
                dshown(holes,"yYY");
            }

            // compute ridge orientations
            if(strchr(ftypes,'r')) {
                float rsmooth = pgetf("ridge_pre_smooth");
                float rpsmooth = pgetf("ridge_post_smooth");
                float asigma = pgetf("ridge_asigma");
                float mpower = pgetf("ridge_mpower");
                ridgemap(maps,binarized,rsmooth,asigma,mpower,rpsmooth);
                dshown(maps(0),"Yyy");
                dshown(maps(1),"YyY");
                dshown(maps(2),"YYy");
                dshown(maps(3),"YYY");
            }

            // compute troughs
            if(strchr(ftypes,'t')) {
                float rsmooth = pgetf("ridge_pre_smooth");
                compute_troughs(troughs,binarized,rsmooth);
                dshown(troughs,"yYY");
            }

            // compute different distance transforms
            if(strchr(ftypes,'D') || strchr(ftypes,'G') || strchr(ftypes,'M')) {
                const char *which = pget("dt_which");
                if(!strcmp(which,"outside")) {
                    dt = binarized;
                    brushfire_2(dt);
                    dt /= absfractile_nz(dt);
                    ssigmoid(dt);
                } else if(!strcmp(which,"inside")) {
                    dt = binarized;
                    sub(max(dt),dt);
                    brushfire_2(dt);
                    dt /= absfractile_nz(dt);
                    ssigmoid(dt);
                } else if(!strcmp(which,"both")) {
                    dt = binarized;
                    sub(max(dt),dt);
                    brushfire_2(dt);
                    // pick scale according to inside only
                    float scale = absfractile_nz(dt);
                    floatarray mdt;
                    mdt = binarized;
                    brushfire_2(mdt);
                    dt -= mdt;
                    dt /= scale;
                    ssigmoid(dt);
                }
                debugf("sfmaprange","dt %g %g\n",min(dt),max(dt));
                if(strchr(ftypes,'G') || strchr(ftypes,'M')) {
                    floatarray smoothed;
                    smoothed = dt;
                    float s = pgetf("dt_grad_smooth");
                    gauss2d(smoothed,s,s);
                    makelike(dt_x,smoothed);
                    makelike(dt_y,smoothed);
                    dt_x = 0;
                    dt_y = 0;
                    for(int i=1;i<smoothed.dim(0)-1;i++) {
                        for(int j=1;j<smoothed.dim(1)-1;j++) {
                            float dx = smoothed(i,j) - smoothed(i-1,j);
                            float dy = smoothed(i,j) - smoothed(i,j-1);
                            dt_x(i,j) = dx;
                            dt_y(i,j) = dy;
                        }
                    }
                    float n = max(1e-5,max(max(dt_x),max(dt_y)));
                    dt_x /= n;
                    dt_y /= n;
                    ssigmoid(dt_x);
                    ssigmoid(dt_y);
                    checknan(dt_x);
                    checknan(dt_y);
                    dshown(dt_x,"YyY");
                    dshown(dt_y,"YYy");
                    debugf("sfmaprange","dt_x %g %g\n",min(dt),max(dt_x));
                    debugf("sfmaprange","dt_y %g %g\n",min(dt),max(dt_y));
                }
                // split the distance transform gradient into separate
                // maps for positive and negative
                apow(dt,pgetf("dt_power"));
                dshown(dt,"Yyy");
                if(strchr(ftypes,'M')) {
                    dt_maps.resize(4);
                    dt_maps(0) = dt_x;
                    heaviside(dt_maps(0));
                    dt_maps(1) = dt_x;
                    negheaviside(dt_maps(1));
                    dt_maps(2) = dt_y;
                    heaviside(dt_maps(2));
                    dt_maps(3) = dt_y;
                    negheaviside(dt_maps(3));
                    dshown(dt_maps(0),"Yyy");
                    dshown(dt_maps(1),"YyY");
                    dshown(dt_maps(2),"YYy");
                    dshown(dt_maps(3),"YYY");
                }
            }

            dwait();
        }

        void append(floatarray &v,floatarray &x,const char *name) {
            for(int i=0;i<x.length();i++)
                v.push(x[i]);
        }

        void extractFeatures(floatarray &v,rectangle b,bytearray &mask) {
            dsection("features");
            b.shift_by(pad,pad);

            CHECK(b.width()==mask.dim(0) && b.height()==mask.dim(1));
            const char *ftypes = pget("ftypes");
            floatarray u;
            v.clear();
            if(strchr(ftypes,'b')) {
                extractFeatures(u,b,mask,binarized);
                CHECK(min(u)>=-1.1 && max(u)<=1.1);
                append(v,u,"b");
            }
            if(strchr(ftypes,'h')) {
                extractFeatures(u,b,mask,holes,false);
                CHECK(min(u)>=-1.1 && max(u)<=1.1);
                append(v,u,"h");
            }
            if(strchr(ftypes,'j')) {
                extractFeatures(u,b,mask,junctions);
                CHECK(min(u)>=-1.1 && max(u)<=1.1);
                append(v,u,"j");
            }
            if(strchr(ftypes,'e')) {
                extractFeatures(u,b,mask,endpoints);
                CHECK(min(u)>=-1.1 && max(u)<=1.1);
                append(v,u,"e");
            }
            if(strchr(ftypes,'r')) {
                floatarray temp;
                for(int i=0;i<maps.length();i++) {
                    extractFeatures(u,b,mask,maps(i));
                    push_array(temp,u);
                }
                CHECK(min(temp)>=-1.1 && max(temp)<=1.1);
                append(v,temp,"r");
            }
            if(strchr(ftypes,'t')) {
                extractFeatures(u,b,mask,troughs);
                CHECK(min(u)>=-1.1 && max(u)<=1.1);
                append(v,u,"t");
            }
            if(strchr(ftypes,'D')) {
                extractFeatures(u,b,mask,dt);
                CHECK(min(u)>=-1.1 && max(u)<=1.1);
                append(v,u,"D");
            }
            if(strchr(ftypes,'G')) {
                extractFeatures(u,b,mask,dt_x);
                CHECK(min(u)>=-1.1 && max(u)<=1.1);
                append(v,u,"Gx");
                extractFeatures(u,b,mask,dt_y);
                CHECK(min(u)>=-1.1 && max(u)<=1.1);
                append(v,u,"Gy");
            }
            if(strchr(ftypes,'M')) {
                floatarray temp;
                for(int i=0;i<dt_maps.length();i++) {
                    extractFeatures(u,b,mask,dt_maps(i));
                    push_array(temp,u);
                }
                CHECK(min(temp)>=-1.1 && max(temp)<=1.1);
                append(v,temp,"M");
            }
        }

        template <class S>
        void extractFeatures(floatarray &v,rectangle b,bytearray &mask,
                             narray<S> &source,bool masked=true) {
            rectangle bp = rectangle(b.x0+pad,b.y0+pad,b.x1+pad,b.y1+pad);
            if(pgetf("aa")>=0) {
                extractFeaturesAA(v,b,mask,source,masked);
            } else {
                extractFeaturesNonAA(v,b,mask,source,masked);
            }
        }

        template <class S>
        void extractFeaturesAA(floatarray &v,rectangle b,bytearray &mask,
                             narray<S> &source,bool masked=true) {
            float scontext = pgetf("scontext");
            int csize = pgetf("csize");
            CHECK(mask.dim(0)==b.width() && mask.dim(1)==b.height());
            CHECK_ARG(v.dim(1)<pgetf("maxheight"));
            if(b.height()>=pgetf("maxheight")) {
                throwf("bbox height %d >= maxheight %g",
                       b.height(),pgetf("maxheight"));
            }

            floatarray sub(b.width(),b.height());
            get_rectangle(sub,source,b);
            float s = max(sub.dim(0),sub.dim(1))/float(csize);
            float sig = s * pgetf("aa");
            if(sig>0) gauss2d(sub,sig,sig);
            bytearray dmask;
            dmask = mask;
            if(int(sig)>0) binary_dilate_circle(dmask,int(sig));

            v.resize(csize,csize);
            v = 0;
            for(int i=0;i<csize;i++) {
                for(int j=0;j<csize;j++) {
                    int mval = bat(dmask,i*s,j*s,0);
                    float value = bilin(sub,i*s,j*s);
                    if(masked && !mval) value = scontext * value;
                    v(i,j) = value;
                }
            }

            float maxval = max(fabs(max(v)),fabs(min(v)));
            if(maxval>1.0) v /= maxval;
            checknan(v);
            CHECK(min(v)>=-1.1 && max(v)<=1.1);

            dsection("dfeats");
            dshown(sub,"a");
            dshown(dmask,"b");
            {floatarray temp; temp = sub; temp -= dmask; dshown(temp,"d");}
            dshown(v,"c");
            dwait();
        }

        template <class S>
        void extractFeaturesNonAA(floatarray &v,rectangle b,bytearray &mask,
                             narray<S> &source,bool masked=true) {
            // FIXME bool use_centroid = pgetf("use_centroid");
            float context = pgetf("context");
            float scontext = pgetf("scontext");
            int csize = pgetf("csize");
            float xc = b.xcenter();
            float yc = b.ycenter();
            int xm = mask.dim(0)/2;
            int ym = mask.dim(1)/2;
            int r = context*max(b.width(),b.height());
            v.resize(csize,csize);
            v = 0;
            for(int i=0;i<csize;i++) {
                for(int j=0;j<csize;j++) {
                    float x = (i*1.0/csize)-0.5;
                    float y = (j*1.0/csize)-0.5;
                    int m = bat(mask,int(x*r+xm),int(y*r+ym),0);
                    float value = bilin(source,x*r+xc,y*r+yc);
                    if(masked) {
                        if(!m) value = scontext * value;
                    }
                    v(i,j) = value;
                }
            }
            float maxval = max(fabs(max(v)),fabs(min(v)));
            if(maxval>1.0) v /= maxval;
            checknan(v);
            CHECK(min(v)>=-1.1 && max(v)<=1.1);
            dsection("dfeats");
            dshown(v,"a");
            dshown(mask,"b");
            dwait();
        }
    };

    void init_glfmaps() {
        static bool init = false;
        if(init) return;
        init = true;
        component_register<SimpleFeatureMap>("SimpleFeatureMap");
#ifndef OBSOLETE
        component_register<SimpleFeatureMap>("sfmap");
#endif
    }
}
