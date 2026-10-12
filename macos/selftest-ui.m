/* Native AppKit regression checks only in the disposable smoke-test root. */
#import <AppKit/AppKit.h>
#include "putty.h"
#include "portable.h"
#include "language.h"
#include "utilities.h"
#include <stdlib.h>
int kr_mac_self_test_ui(void){
    const char *enabled=getenv("KITTY_NG_SELF_TEST"),*root=kr_mac_root();
    if(!enabled||strcmp(enabled,"1")||!root||!strstr(root,"/kitty-ng-smoke.")||(strncmp(root,"/tmp/",5)&&strncmp(root,"/private/tmp/",13)))return 2;
    NSApplication *app=NSApplication.sharedApplication;app.activationPolicy=NSApplicationActivationPolicyAccessory;
    NSView *view=[[NSView alloc]initWithFrame:NSMakeRect(0,0,600,200)];
    NSSearchField *search=[[NSSearchField alloc]initWithFrame:NSMakeRect(0,0,300,30)];search.stringValue=@"demo query";[view addSubview:search];
    NSPopUpButton *picker=[[NSPopUpButton alloc]initWithFrame:NSMakeRect(0,50,150,30) pullsDown:NO];[picker addItemsWithTitles:@[@"English",@"Русский"]];[picker selectItemAtIndex:1];[view addSubview:picker];
    NSTextField *comment=[[NSTextField alloc]initWithFrame:NSMakeRect(0,100,300,30)];comment.stringValue=@"unchanged comment";comment.placeholderString=@"Key comment";[view addSubview:comment];
    NSButtonCell *cell=((NSSearchFieldCell*)search.cell).searchButtonCell;NSString *title=[cell.title copy];NSImage *image=cell.image;
#define CHECK(c) do{if(!(c)){fprintf(stderr,"FAIL: %s\n",#c);return 1;}}while(0)
    for(int round=0;round<6;round++){int language=round%2;CHECK(kr_mac_set_language(language));kr_mac_localize_view(view);
        CHECK([search.stringValue isEqualToString:@"demo query"]);CHECK([search.placeholderString isEqualToString:language?@"Быстрый поиск":@"Quick search"]);
        CHECK([comment.stringValue isEqualToString:@"unchanged comment"]);CHECK([comment.placeholderString isEqualToString:language?@"Комментарий ключа":@"Key comment"]);
        CHECK([cell.title isEqualToString:title]&&cell.image==image);CHECK([picker itemAtIndex:0].title&&[[picker itemAtIndex:0].title isEqualToString:@"English"]);CHECK([[picker itemAtIndex:1].title isEqualToString:@"Русский"]);CHECK(picker.indexOfSelectedItem==1);
    }
    Conf *conf=conf_new();load_settings("Default Settings",conf);CHECK(conf_get_int(conf,CONF_protocol)==PROT_SSH);CHECK(conf_get_int(conf,CONF_port)==22);conf_free(conf);
    CHECK(kr_mac_test_keygen_ui()==0);
    puts("PASS: native AppKit language round-trips, search cell/data preserved, stable language names, initial SSH/22 defaults");return 0;
#undef CHECK
}
