/* Limit a one-shot preview bitmap to 32 MiB; no framebuffer/context setting. */
static int dream_poster_preview_bounds(double x,double y,double width,double height,double scale) {
    if(!isfinite(x)||!isfinite(y)||!isfinite(width)||!isfinite(height)||!isfinite(scale))return 0;
    if(width<=0||height<=0||scale<=0||scale>4)return 0;
    double w=ceil(width*scale),h=ceil(height*scale);
    return isfinite(w)&&isfinite(h)&&w>=1&&h>=1&&w<=8192&&h<=8192&&w*h<=8388608;
}
