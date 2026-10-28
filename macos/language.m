/* Same portable UILanguage preference as Windows, in values.ini. */
#import <AppKit/AppKit.h>
#include "putty.h"
#include "storage.h"
#include "portable.h"
#include "language.h"
#include "kittyng-i18n.h"
#import <objc/runtime.h>
#include "cli-language.h"
static int selected=-1;
static NSString *preferences(NSStringEncoding *encoding){
    *encoding=NSUTF8StringEncoding;
    const char *base=kr_mac_root();if(!base)return @"";NSString *path=[[NSString stringWithUTF8String:base]stringByAppendingPathComponent:@"values.ini"];NSData *data=[NSData dataWithContentsOfFile:path];if(!data){*encoding=NSUTF8StringEncoding;return @"";}
    const unsigned char *p=data.bytes;*encoding=data.length>=2&&p[0]==255&&p[1]==254?NSUTF16LittleEndianStringEncoding:NSUTF8StringEncoding;
    NSString *value=[[NSString alloc]initWithData:data encoding:*encoding];if(!value){*encoding=NSWindowsCP1251StringEncoding;value=[[NSString alloc]initWithData:data encoding:*encoding];}if([value hasPrefix:@"\uFEFF"])value=[value substringFromIndex:1];return value;
}
int kr_mac_language(void){
    if(selected>=0)return selected;selected=[[[NSLocale preferredLanguages]firstObject]hasPrefix:@"ru"]?1:0;
    NSStringEncoding encoding;NSString *value=preferences(&encoding);BOOL active=NO;
    for(NSString *raw in [value componentsSeparatedByCharactersInSet:NSCharacterSet.newlineCharacterSet]){NSString *line=[raw stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];if([line hasPrefix:@"["])active=[line.lowercaseString isEqualToString:@"[values]"];else if(active){NSRange equals=[line rangeOfString:@"="];if(equals.location!=NSNotFound&&[[[line substringToIndex:equals.location]stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet]isEqualToString:@"UILanguage"]){NSString *choice=[[line substringFromIndex:equals.location+1]stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];if([choice isEqualToString:@"0"]||[choice isEqualToString:@"1"])selected=choice.intValue;}}}
    const char *override=getenv("KITTY_NG_LANGUAGE");if(override){if(!strcmp(override,"ru"))selected=1;else if(!strcmp(override,"en"))selected=0;}    NSArray *arguments=NSProcessInfo.processInfo.arguments;for(NSUInteger i=1;i+1<arguments.count;i++)if([arguments[i]isEqualToString:@"--language"]){if([arguments[i+1]isEqualToString:@"ru"])selected=1;else if([arguments[i+1]isEqualToString:@"en"])selected=0;}
return selected;
}
bool kr_mac_set_language(int language){
    if(language!=0&&language!=1)return false;const char *base=kr_mac_root();if(!base||strstr(base,"/AppTranslocation/"))return false;
    NSStringEncoding encoding;NSString *old=preferences(&encoding);if(!old)return false;NSMutableString *out=[NSMutableString new];BOOL active=NO,written=NO,section=NO;
    for(NSString *raw in [old componentsSeparatedByString:@"\n"]){NSString *line=[raw stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];if([line hasPrefix:@"["]){if(active&&!written){[out appendFormat:@"UILanguage=%d\n",language];written=YES;}active=[line.lowercaseString isEqualToString:@"[values]"];section|=active;}
        NSRange equal=[line rangeOfString:@"="];if(active&&equal.location!=NSNotFound&&[[[line substringToIndex:equal.location]stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet]isEqualToString:@"UILanguage"]){if(!written){[out appendFormat:@"UILanguage=%d\n",language];written=YES;}}else{[out appendString:raw];[out appendString:@"\n"];}}
    if(!written){if(!section)[out appendString:@"[values]\n"];[out appendFormat:@"UILanguage=%d\n",language];}
    NSData *data=[out dataUsingEncoding:encoding];if(!data)return false;
    if(encoding==NSUTF16LittleEndianStringEncoding&&!(data.length>=2&&((const unsigned char*)data.bytes)[0]==255&&((const unsigned char*)data.bytes)[1]==254)){NSMutableData *bom=[NSMutableData dataWithBytes:"\xff\xfe" length:2];[bom appendData:data];data=bom;}
    NSString *path=[[NSString stringWithUTF8String:base]stringByAppendingPathComponent:@"values.ini"];if(![data writeToFile:path options:NSDataWritingAtomic error:nil])return false;selected=language;kr_mac_cli_set_language(language?"ru":"en");return true;
}

static char localizedSourceKey;
NSString *kr_mac_localize_string(NSString *text){
    if (!text) return nil;
    if ([text isEqualToString:@"English"] || [text isEqualToString:@"Русский"]) return text;
    NSString *source=objc_getAssociatedObject(text,&localizedSourceKey);
    const char *key=source ? source.UTF8String : kr_i18n_source(KR_TEXT_MAC_UI,text.UTF8String);
    NSString *result=[NSString stringWithUTF8String:kr_i18n_translate(kr_mac_language(),KR_TEXT_MAC_UI,key)];
    /* Immutable string copies retain this source through AppKit setters. This
     * disambiguates e.g. Edit/Modify without reverse-translating visible text. */
    objc_setAssociatedObject(result,&localizedSourceKey,[NSString stringWithUTF8String:key],OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    return result;
}
/* Retain source keys on application-owned UI properties, independently of the
 * displayed language. Editable contents and server/user data never bind here. */
static char sourceKeys;
static NSString *boundText(id owner, NSString *property, NSString *text) {
    if (!text) return nil;
    if ([text isEqualToString:@"English"] || [text isEqualToString:@"Русский"]) return text;
    NSMutableDictionary *keys=objc_getAssociatedObject(owner,&sourceKeys);
    if (!keys) {
        keys=[NSMutableDictionary dictionary];
        objc_setAssociatedObject(owner,&sourceKeys,keys,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    }
    NSString *key=keys[property];
    if (!key) {
        key=objc_getAssociatedObject(text,&localizedSourceKey) ?: [NSString stringWithUTF8String:kr_i18n_source(KR_TEXT_MAC_UI,text.UTF8String)];
        keys[property]=key;
    }
    return [NSString stringWithUTF8String:kr_i18n_translate(kr_mac_language(),KR_TEXT_MAC_UI,key.UTF8String)];
}
void kr_mac_localize_menu(NSMenu *menu){
    menu.title=boundText(menu,@"title",menu.title);
    for(NSMenuItem *item in menu.itemArray){if([item.identifier isEqualToString:@"KiTTYNG.RawData"])continue;item.title=boundText(item,@"title",item.title);if(item.submenu)kr_mac_localize_menu(item.submenu);}
}
void kr_mac_localize_view(NSView *view){
    if([view.identifier isEqualToString:@"KiTTYNG.RawData"])return;
    /* Never recurse into AppKit controls: their private buttons/cells have
     * implementation titles (e.g. search), not application UI strings. */
    if([view isKindOfClass:NSSearchField.class]){((NSSearchField*)view).placeholderString=kr_mac_localize_string(@"Quick search");return;}
    if([view isKindOfClass:NSPopUpButton.class]){for(NSMenuItem *item in ((NSPopUpButton*)view).itemArray)item.title=boundText(item,@"title",item.title);return;}
    if([view isKindOfClass:NSButton.class]){((NSButton*)view).title=boundText(view,@"title",((NSButton*)view).title);return;}
    if([view isKindOfClass:NSTextField.class]){NSTextField *field=(NSTextField*)view;if(field.placeholderString)field.placeholderString=boundText(field,@"placeholder",field.placeholderString);if(!((NSTextField*)view).editable)((NSTextField*)view).stringValue=kr_mac_localize_string(((NSTextField*)view).stringValue);return;}
    if([view isKindOfClass:NSTableView.class]){for(NSTableColumn *column in ((NSTableView*)view).tableColumns)column.title=boundText(column,@"title",column.title);return;}
    if([view isKindOfClass:NSTabView.class]){for(NSTabViewItem *item in ((NSTabView*)view).tabViewItems){item.label=boundText(item,@"label",item.label);kr_mac_localize_view(item.view);}return;}
    if([view isKindOfClass:NSControl.class])return;
    for(NSView *child in view.subviews)kr_mac_localize_view(child);
}

void kr_mac_add_edit_menu(NSMenu *menu){
NSMenuItem *editItem=[[NSMenuItem alloc]initWithTitle:kr_mac_localize_string(@"Edit") action:NULL keyEquivalent:@""];[menu addItem:editItem];
    NSMenu *editMenu=[[NSMenu alloc]initWithTitle:kr_mac_localize_string(@"Edit")];editItem.submenu=editMenu;
    [editMenu addItemWithTitle:kr_mac_localize_string(@"Cut") action:@selector(cut:) keyEquivalent:@"x"];
    [editMenu addItemWithTitle:kr_mac_localize_string(@"Copy") action:@selector(copy:) keyEquivalent:@"c"];
    [editMenu addItemWithTitle:kr_mac_localize_string(@"Paste") action:@selector(paste:) keyEquivalent:@"v"];
    [editMenu addItemWithTitle:kr_mac_localize_string(@"Select all") action:@selector(selectAll:) keyEquivalent:@"a"];
}
