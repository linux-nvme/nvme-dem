import { Injectable } from '@angular/core';
import { HttpClient } from '@angular/common/http';
import { Observable } from 'rxjs';
import {Hosts} from './interface';

interface myData {
  message: string,
  success: boolean
}

interface isLoggedIn {
  status: boolean
}

interface logoutStatus {
  success: boolean
}
@Injectable({
  providedIn: 'root'
})
export class UserService {

  constructor(private http: HttpClient) { }

  currentTarget: string = "";
  currentIPAddress: string = "";
  currentIPPort: string = "";
  currentTargetTransportIP : string = "";
  currentSubSystem : string = "";
  currentConnection: string = "";
  currentController : string = "";
  currentEndPoint: string = "";
  
  currentVolume: string = "";
  currentCapacitySourcePP: string = "";
  currentCapacitySourceSPC: string = "";
  currentCapacitySourceSP: string = "";

  currentSystemName: string = ""
  currentSystemStorageName: string = ""
  currentChassisName: string = ""
  currentNetworkAdapterName: string = ""
  currentNetworkDeviceFunctionName: string = ""
  currentEthernetInterfaceName: string = ""

  contollerList : string[] = []
  volumeList : string[] = []
  endpointList : string[] = []
  transportIPList : string[] = []
  connectionList : Array<{targetEP: any, initiatorEP: any, volumeInfo: any, accessCapabilityL: any}> = []
  volNSList : any[] = []

  url = "https://8cdab594-aa27-46d4-a50b-d3f4cb1be2b4.mock.pstmn.io/";

  addNewTargetServer(data: any) {
    return this.http.post<any>(this.url + this.currentIPAddress + ':' + this.currentIPPort + '/redfish/v1/targets_systems/add', data)
  }

  editTargetServer(data:any, id: any){
    return this.http.put<any>(this.url + this.currentIPAddress + ':' + this.currentIPPort + '/redfish/v1/targets_systems/edit/' +  id, data.value)
  }

  deleteTargetServer(id:any){
    return this.http.delete<any>(this.url + this.currentIPAddress + ':' + this.currentIPPort + '/redfish/v1/targets_systems/delete/' +  id)
  }

  getLoginInfo() {
    return this.http.get(this.url + '/login')   
  }

  isLoggedIn(): Observable<isLoggedIn> {
    return this.http.get<isLoggedIn>(this.url + 'isLoggedIn')
  }

  logout() {
    return this.http.get<logoutStatus>('/api/logout.php')
  }

  getHostList() {
    return this.http.get<any>(this.url + this.currentIPAddress + ':' + this.currentIPPort + '/redfish/v1/Systems')
  }

 getTargetList() {
  return this.http.get<any>(this.url + this.currentIPAddress + ':' + this.currentIPPort + '/redfish/v1/targets_systems')
}

getSubSystemList() {
  return this.http.get<any>(this.url + this.currentTargetTransportIP + '/redfish/v1/Storage')
}

getSubSystemDetails() {
  return this.http.get<any>(this.url + this.currentTargetTransportIP + '/redfish/v1/Storage/' + this.currentSubSystem)
}

getSubSystemVolume() {
  return this.http.get<any>(this.url + this.currentTargetTransportIP + '/redfish/v1/Storage/' + this.currentSubSystem + '/Volumes')
}

getSubSystemCapacitySourcePP() {
  return this.http.get<any>(this.url + this.currentTargetTransportIP + '/redfish/v1/Storage/' + this.currentSubSystem + '/Volumes/' + this.currentVolume + '/CapacitySources')
}

getSubSystemPP() {
  return this.http.get<any>(this.url + this.currentTargetTransportIP + '/redfish/v1/Storage/' + this.currentSubSystem + '/Volumes/' + this.currentVolume + '/CapacitySources/' + this.currentCapacitySourcePP + '/ProvidingPools')
}

getSubSystemCapacitySourceSP() {
  return this.http.get<any>(this.url + this.currentTargetTransportIP + '/redfish/v1/Storage/' + this.currentSubSystem + '/StoragePools/' + this.currentCapacitySourceSPC + '/CapacitySources')
}

getSubSystemVolumeNS() {
  return this.http.get<any>(this.url + this.currentTargetTransportIP + '/redfish/v1/Storage/' + this.currentSubSystem + '/StoragePools/' + this.currentCapacitySourceSPC + '/CapacitySources/' + this.currentCapacitySourceSP + '/ProvidingVolumes')
}

getSubSystemControllers() {
  return this.http.get<any>(this.url + this.currentTargetTransportIP + '/redfish/v1/Storage/' + this.currentSubSystem + '/Controllers')
}

getSubSystemControllersDetails() {
  return this.http.get<any>(this.url + this.currentTargetTransportIP + '/redfish/v1/Storage/' + this.currentSubSystem + '/Controllers/' + this.currentController)
}

getSubSystemEndPointsDetails() {
  return this.http.get<any>(this.url + this.currentTargetTransportIP + '/redfish/v1/Fabrics/NVMe-oF/Endpoints/' + this.currentEndPoint)
}

getSubSystemConnection() {
  return this.http.get<any>(this.url + this.currentTargetTransportIP + '/redfish/v1/Fabrics/NVMe-oF/Connections')
}

getSubSystemConnectionDetails() {
  return this.http.get<any>(this.url + this.currentTargetTransportIP + '/redfish/v1/Fabrics/NVMe-oF/Connections/' + this.currentConnection)
}

getSystemName() {
  return this.http.get<any>(this.url + this.currentTargetTransportIP + '/redfish/v1/Systems')
}

getSystemStorageName() {
  return this.http.get<any>(this.url + this.currentTargetTransportIP + '/redfish/v1/Systems/' + this.currentSystemName + '/Storage')
}

getSystemVolumeName() {
  return this.http.get<any>(this.url + this.currentTargetTransportIP + '/redfish/v1/Systems/Sys-1/Storage/' + this.currentSystemStorageName + '/Volumes')
}

getChasisName() {
  return this.http.get<any>(this.url + this.currentTargetTransportIP + '/redfish/v1/Chassis')
}

getNetworkAdapterName() {
  return this.http.get<any>(this.url + this.currentTargetTransportIP + '/redfish/v1/Chassis/Sys-1Chassis/NetworkAdapters')
}

getNetworkDeviceFunction() {
  return this.http.get<any>(this.url + this.currentTargetTransportIP + '/redfish/v1/Chassis/Sys-1Chassis/NetworkAdapters/1/NetworkDeviceFunctions')
}

getEthernetInterfaces() {
  return this.http.get<any>(this.url + this.currentTargetTransportIP + '/redfish/v1/Chassis/Sys-1Chassis/NetworkAdapters/1/NetworkDeviceFunctions/1/EthernetInterfaces')
}

getChassisTransportIPDetails() {
  return this.http.get<any>(this.url + this.currentTargetTransportIP + '/redfish/v1/Chassis/Sys-1Chassis/NetworkAdapters/1/NetworkDeviceFunctions/1/EthernetInterfaces/1')
}

getHostTableHeaders() {
  return ["Host Name", "Actions"];
}

getHostTableDataHeaders() {
 return ["Name"];
}

getTargetTableHeaders() {
  return ["Target Name", "Transport IP", "Actions"];
}

getTargetTableDataHeaders() {
 return ["Name", "Transport_IP"];
}

getSubSystemTableHeaders() {
  return ["SubSystem Name", "Actions"];
}

getSubSystemTableDataHeaders() {
 return ["Name"];
}

setVolumeNS(volNSList: any[]) {
  this.volNSList = volNSList;
}

getVolumeNS() : any[] {
  return this.volNSList;
}

getControllerList() : string[] {
  return this.contollerList;
}

setControllerList(cList : string[]) {
  this.contollerList = cList;
}

getVolumeList() : string[] {
  return this.volumeList;
}

setVolumeList(vList : string[]) {
  this.volumeList = vList;
}

getEndpointList() : string[] {
  return this.endpointList;
}

setEndpointList(eList : string[]) {
  this.endpointList = eList;
}

getTransportIPList() : string[] {
  return this.transportIPList;
}

setTransportIPList(tList : string[]) {
  this.transportIPList = tList;
}

getConnectionList() : Array<{targetEP: any, initiatorEP: any, volumeInfo: any, accessCapabilityL: any}> {
  return this.connectionList;
}

setConnectionList(connList : Array<{targetEP: any, initiatorEP: any, volumeInfo: any, accessCapabilityL: any}>) {
  this.connectionList = connList;
}

setCurrentChassisName(currentChassisName: string) : void {
  this.currentChassisName = currentChassisName;
}

getCurrentChassisName() : string {
return this.currentChassisName;
}

setCurrentNetworkAdapterName(currentNetworkAdapterName: string) : void {
  this.currentNetworkAdapterName = currentNetworkAdapterName;
}

getCurrentNetworkAdapterName() : string {
return this.currentNetworkAdapterName;
}

setCurrentNetworkDeviceFunctionName(currentNetworkDeviceFunctionName: string) : void {
  this.currentNetworkDeviceFunctionName = currentNetworkDeviceFunctionName;
}

getCurrentNetworkDeviceFunctionName() : string {
return this.currentNetworkDeviceFunctionName;
}

setCurrentEthernetInterfaceName(currentEthernetInterfaceName: string) : void {
  this.currentEthernetInterfaceName = currentEthernetInterfaceName;
}

getCurrentEthernetInterfaceName() : string {
return this.currentEthernetInterfaceName;
}

setCurrentSystemName(currentSystemName: string) : void {
  this.currentSystemName = currentSystemName;
}

getCurrentSystemName() : string {
return this.currentSystemName;
}

setCurrentSystemStorageName(currentSystemStorageName: string) : void {
  this.currentSystemStorageName = currentSystemStorageName;
}

getCurrentSystemStorageName() : string {
return this.currentSystemStorageName;
}

setCurrentTarget(currentTarget: string) : void {
   this.currentTarget = currentTarget;
}

getCurrentTarget() : string {
return this.currentTarget;
}

setUserIPAddress(currentIPAddress: string) : void {
  this.currentIPAddress = currentIPAddress;
}

getUserIPAddress() : string {
return this.currentIPAddress;
}

setUserIPPort(currentIPPort: string) : void {
  this.currentIPPort = currentIPPort;
}

getUserIPPort() : string {
return this.currentIPPort;
}

setCurrentTargetTransportIP(currentTargetTransportIP: string) : void {
  this.currentTargetTransportIP = currentTargetTransportIP;
}

getCurrentTargetTransportIP() : string {
return this.currentTargetTransportIP;
}

setCurrentVolume(currentVolume: string) : void {
  this.currentVolume = currentVolume;
}

getCurrentVolume() : string {
return this.currentVolume;
}

setCurrentCapacitySourcePP(currentCapacitySourcePP: string) : void {
  this.currentCapacitySourcePP = currentCapacitySourcePP;
}

getCurrentCapacitySourcePP() : string {
return this.currentCapacitySourcePP;
}

setCurrentCapacitySourceSPC(currentCapacitySourceSPC: string) : void {
  this.currentCapacitySourceSPC = currentCapacitySourceSPC;
}

getCurrentCapacitySourceSPC() : string {
return this.currentCapacitySourceSPC;
}

setCurrentCapacitySourceSP(currentCapacitySourceSP: string) : void {
  this.currentCapacitySourceSP = currentCapacitySourceSP;
}

getCurrentCapacitySourceSP() : string {
return this.currentCapacitySourceSP;
}

setCurrentController(currentController: string) : void {
  this.currentController = currentController;
}

getCurrentController() : string {
return this.currentController;
}

setCurrentConnectionName(currentConnection: string) : void {
  this.currentConnection = currentConnection;
}

getCurrentConnectionName() : string {
return this.currentConnection;
}


setCurrentSubSystem(currentSubSystem: string) : void {
  this.currentSubSystem = currentSubSystem;
}

getCurrentSubSystem() : string {
return this.currentSubSystem;
}

setCurrentEndPoint(currentEndPoint: string) : void {
  this.currentEndPoint = currentEndPoint;
}

getCurrentEndPoint() : string {
return this.currentEndPoint;
}


}
