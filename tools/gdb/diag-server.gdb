# 診断: server 側の設定とブレーク一覧を出させる(出力は jobs/server.out.log)。書込みなし。nonstop:true
set pagination off
set confirm off
monitor reset
monitor printBkPts
monitor printSettings
monitor is_target_connected
monitor is_target_stopped,0
maint packet vCont;c:p1.1
python import time; time.sleep(1.0)
monitor printBkPts
monitor getPC
detach
