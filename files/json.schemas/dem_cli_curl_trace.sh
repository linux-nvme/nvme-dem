exec_dem() {
  echo dem $*
  echo "------------------------" >>dem_cli_curl_trace.log
  echo dem $* >>dem_cli_curl_trace.log
  echo "------------------------" >>dem_cli_curl_trace.log
  dem -rcf $* >>dem_cli_curl_trace.log
}
echo -n >dem_cli_curl_trace.log

exec_dem config
exec_dem list group
exec_dem add group g1
exec_dem set group g2
exec_dem list group
exec_dem get group g1
exec_dem delete group g1
exec_dem rename group g2 g1
exec_dem add target t1
exec_dem set target t2
exec_dem list target
exec_dem get target t1
exec_dem delete target t1
exec_dem rename target t2 t1
exec_dem set mode t1 local
exec_dem set interface t1 ipv4 1.1.1.1 4422
exec_dem set refresh t1 0
exec_dem refresh target t1
exec_dem link target t1 g1
exec_dem get group g1
exec_dem unlink target t1 g1
exec_dem add subsystem t1 subsys2
exec_dem set subsystem t1 subsys1 0
exec_dem edit subsystem t1 subsys1 0
exec_dem delete subsystem t1 subsys1
exec_dem rename subsystem t1 subsys2 subsys1
exec_dem set portid t1 1 rdma ipv4 1.1.1.1 4420
exec_dem edit portid t1 1 rdma ipv4 1.1.1.1 4420
exec_dem delete portid t1 1
exec_dem set portid t1 1 rdma ipv4 1.1.1.1 4420
exec_dem set host h1 hostnqn
exec_dem set acl t1 subsys1 h1
exec_dem delete acl t1 subsys1 h1
exec_dem delete host h1
exec_dem set ns t1 subsys1 1 nullb0 0
exec_dem get target t1
exec_dem delete ns t1 subsys1 1
exec_dem add host h1
exec_dem list host
exec_dem get host h1
exec_dem set host h2 hostnqn
exec_dem edit host h2 hostnqn
exec_dem get host h1
exec_dem delete host h1
exec_dem rename host h2 h1 
exec_dem link host h1 g1
exec_dem get group g1
exec_dem unlink host h1 g1
exec_dem shutdown
