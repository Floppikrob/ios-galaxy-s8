/* Optional display hint, published after authoritative original encoding.
 * Failed/unsupported service keeps the existing original-pixel display path.
 */
static io_connect_t dream_mirror_connection;
static pthread_once_t dream_mirror_once=PTHREAD_ONCE_INIT;
static __attribute__((minsize)) void dream_open_display_mirror(void)
{
    io_service_t service=IOServiceGetMatchingService(0,IOServiceMatching("HakiosMali"));
    if(service){(void)IOServiceOpen(service,mach_task_self(),0,&dream_mirror_connection);IOObjectRelease(service);}
}
static __attribute__((minsize)) void
dream_offer_display_mirror(IOSurfaceRef original,IOSurfaceRef mirror,unsigned w,unsigned h)
{
    pthread_once(&dream_mirror_once,dream_open_display_mirror);
    if(!dream_mirror_connection)return;
    uint32_t q[8]={IOSurfaceGetID(original),IOSurfaceGetID(mirror),IOSurfaceGetSeed(original),IOSurfaceGetSeed(mirror),w,h,0,0};
    uint32_t reply[8];size_t n=sizeof(reply);
    (void)IOConnectCallStructMethod(dream_mirror_connection,0x81,q,sizeof(q),reply,&n);
}
