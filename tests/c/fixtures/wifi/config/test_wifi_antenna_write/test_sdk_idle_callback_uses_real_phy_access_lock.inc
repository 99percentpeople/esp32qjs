
int main(void){int error=-77;
 assert(esp32qjs_phy_antenna_run_idle(NULL,NULL)==1&&!s_phy_access_lock);
 awake=1;assert(esp32qjs_phy_antenna_run_idle(apply,&error)==2&&!called&&!s_phy_access_lock);
 awake=0;assert(esp32qjs_phy_antenna_run_idle(apply,&error)==-77&&called==1&&!s_phy_access_lock);
}
