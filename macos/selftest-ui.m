/* Native AppKit regression checks only in the disposable smoke-test root. */
#import <AppKit/AppKit.h>
#include "putty.h"
#include "portable.h"
#include "storage.h"
#include "language.h"
#include "utilities.h"
#include "quick-commands.h"
#include "client.h"
#include "kittyng-auth.h"
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
    app.mainMenu=[NSMenu new];kr_mac_add_edit_menu(app.mainMenu);
    NSMenu *edit=app.mainMenu.itemArray.lastObject.submenu;
    CHECK(edit.numberOfItems==4);
    CHECK([edit itemAtIndex:0].action==@selector(cut:));
    CHECK([edit itemAtIndex:1].action==@selector(copy:));
    CHECK([edit itemAtIndex:2].action==@selector(paste:));
    CHECK([edit itemAtIndex:3].action==@selector(selectAll:));
    for(NSMenuItem *item in edit.itemArray)CHECK(item.target==nil&&item.keyEquivalent.length==1);
    for(int language=0;language<2;language++){
        CHECK(kr_mac_set_language(language));kr_mac_localize_menu(app.mainMenu);
        NSString *title=language?@"Изменить":@"Edit";
        CHECK([edit.title isEqualToString:title]);
        CHECK([app.mainMenu.itemArray.lastObject.title isEqualToString:title]);
        CHECK([[edit itemAtIndex:1].title isEqualToString:language?@"Копировать":@"Copy"]);
    }
    Conf *auth=conf_new();load_settings("Default Settings",auth);conf_set_str(auth,CONF_host,"synthetic.invalid");
    prompts_t *prompt=new_prompts();prompt->from_server=false;prompt->name=dupstr("SSH password");add_prompt(prompt,dupstr("Password: "),false);
    prompt_set_result(prompt->prompts[0],"synthetic-runtime-auth-marker");kr_auth_capture(auth,prompt,SPR_OK);
    Conf *copy=kr_auth_duplicate(auth,true);conf_free(auth);CHECK(kr_auth_reuse(copy,prompt));CHECK(!kr_auth_reuse(copy,prompt));
    CHECK(!save_settings("runtime-auth-native-test",copy));Conf *reload=conf_new();CHECK(load_settings("runtime-auth-native-test",reload));CHECK(!*conf_get_str(reload,CONF_kr_auth_password));conf_free(reload);del_settings("runtime-auth-native-test");
    conf_set_int(copy,CONF_kr_auth_attempts,0);prompt->from_server=true;CHECK(!kr_auth_reuse(copy,prompt));prompt->from_server=false;
    conf_set_str(copy,CONF_host,"different.invalid");CHECK(!kr_auth_reuse(copy,prompt));conf_free(copy);free_prompts(prompt);
    puts("PASS: native auth cache survives source close, one attempt, never saved, server challenge and changed identity rejected");
    extern int kr_mac_test_startup_ui(void);
    CHECK(kr_mac_test_startup_ui()==0);
    extern int kr_mac_test_save_window_ui(void);
    CHECK(kr_mac_test_save_window_ui()==0);
    CHECK(kr_mac_test_session_menu_ui()==0);
    CHECK(kr_mac_test_tabs_ui()==0);
    CHECK(kr_mac_test_keygen_ui()==0);
    CHECK(kr_mac_test_quick_commands_ui()==0);
    puts("PASS: native AppKit language round-trips, search cell/data preserved, stable language names, initial SSH/22 defaults");return 0;
#undef CHECK
}
