#include "putty.h"
#include "kittyng.h"
#include "kittyng-tray.h"
HMENU kr_tray_popup_menu(void){
    HMENU menu=CreatePopupMenu();if(!menu)return NULL;
    kr_append_menu(menu,MF_STRING,1,"Restore");
    AppendMenuW(menu,MF_SEPARATOR,0,NULL);
    kr_append_menu(menu,MF_STRING,2,"About");
    AppendMenuW(menu,MF_SEPARATOR,0,NULL);
    kr_append_menu(menu,MF_STRING,3,"Exit");
    SetMenuDefaultItem(menu,1,FALSE);return menu;
}
