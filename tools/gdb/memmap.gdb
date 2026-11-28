# 観察用(書込みなし): サーバーの memory-map に flash 領域があるか(あれば load は vFlashErase/Write/Done を使うはず)。NS=true
set pagination off
set confirm off
maint packet qXfer:memory-map:read::0,2000
maint packet qXfer:memory-map:read::bfb,2000
quit
