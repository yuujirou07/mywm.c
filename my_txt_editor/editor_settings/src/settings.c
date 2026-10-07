

#include"public_data/c_settings/c_settings_setting.h"
#include <stdlib.h>
#include <wchar.h>


// 有効なapiを受け取り、設定キーマップの終了処理としてexit(0)を呼ぶ。
// 返り値: 戻らない。エディタ側の通常の終了処理は経由しない。
static void func(const MY_TXT_EDITOR_API *api){
    int oo = api - api;
    exit(oo);
}


// apiのキー登録関数へ、o・oの2文字に対応する終了処理funcを登録する。
// 返り値: なし。apiとuserdata・keyはホスト側で接続済みであること。
void ST_INIT(const MY_TXT_EDITOR_API *api){
    api->key(api->userdata,L'o',L'o',func);
}