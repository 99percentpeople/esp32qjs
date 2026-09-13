
static void *profile_allocation;
static size_t profile_allocation_bytes;
static unsigned profile_wipes;
static void *eap_profile_alloc(size_t n,size_t size,int caps){
    assert(!critical && !profile_allocation && n==1 && caps==MALLOC_CAP_8BIT);
    void *p=heap_caps_calloc(n,size,caps);
    if(p){profile_allocation=p;profile_allocation_bytes=size;}return p;
}
static void eap_profile_free(void *p){
    assert(!critical && p==profile_allocation);
    for(size_t i=0;i<profile_allocation_bytes;i++)assert(((uint8_t *)p)[i]==0);
    profile_wipes++;profile_allocation=NULL;profile_allocation_bytes=0;heap_caps_free(p);
}
