import { Component, OnInit } from '@angular/core';
import { UserService } from '../user.service';

@Component({
  selector: 'app-subsystem-details',
  templateUrl: './subsystem-details.component.html',
  styleUrls: ['./subsystem-details.component.css']
})
export class SubsystemDetailsComponent implements OnInit {
  targetName : string = "";
  subSystemName : string = "";
  targetTransportIP : string = ""
  subsytemDetailsName : string = ""

  controllerDetails =[];
  volumeDetails = [];
  endpointDetails = [];
  transportIPDetails = []
  connectionNameDetails = []
  connectionDetails = []
  subsystemVolumes = []
  capacitySourcePP = []
  providingPools = []
  providingStoragePools = []
  volumeNS = []
  VolumeNSList =[]

  public connectionDataList: Array<{targetEP: any, initiatorEP: any, volumeInfo: any, accessCapabilityL: any}> = []

  constructor(private user: UserService) { 
    console.log("Constructor");
  }

  ngOnInit(): void {
    document.body.classList.add('bg-admin');

    //selected target server
    this.targetName = this.user.getCurrentTarget();

    //selected transport IP details for the selected target server
    this.targetTransportIP = this.user.getCurrentTargetTransportIP();

    //selected subsystem name for a specific taget server
    this.subSystemName = this.user.getCurrentSubSystem();

    //API call to get the selected Subsystem Name
    this.user.getSubSystemDetails().subscribe( data => {
      console.log("data" + JSON.stringify(data));
      this.subsytemDetailsName = data.Name
    });

    //Some API Calls to get the Volume Namespace
    //API call to get the volumes
    this.user.getSubSystemVolume().subscribe( data => {
      let currentVol = "";
      this.subsystemVolumes = data.Members.map(d => {
        currentVol =  d['@odata.id'].split('/')[(d['@odata.id'].split('/').length) -1];
        this.user.setCurrentVolume(currentVol);

        //API call to get the Capacity Source Providing Pool
        this.user.getSubSystemCapacitySourcePP().subscribe( data => {
          let currentCSPP = "";
          this.capacitySourcePP = data.Members.map(d => {
            currentCSPP =  d['@odata.id'].split('/')[(d['@odata.id'].split('/').length) -1];
            this.user.setCurrentCapacitySourcePP(currentCSPP);

            
            //API call to get Providing Pool having storage Pool name
            this.user.getSubSystemPP().subscribe( data => { 
              let currentPP = "";
              this.providingPools = data.Members.map(d => {
                currentPP =  d['@odata.id'].split('/')[(d['@odata.id'].split('/').length) -1];
                this.user.setCurrentCapacitySourceSPC(currentPP);
                
                //API call to get capacity source storage pool
                this.user.getSubSystemCapacitySourceSP().subscribe( data => { 
                  let currentSPP = "";
                  this.providingStoragePools = data.Members.map(d => {
                    currentSPP =  d['@odata.id'].split('/')[(d['@odata.id'].split('/').length) -1];
                    this.user.setCurrentCapacitySourceSP(currentSPP);

                //API call to get providing volume namespace
                this.user.getSubSystemVolumeNS().subscribe( data => { 
                  let currentVolNS = "";
                  this.volumeNS = data.Members.map(d => {
                    currentVolNS =  d['@odata.id'].split('/')[(d['@odata.id'].split('/').length) -1];

                    return {
                      VolumeNS: currentVolNS,
                      Volume: currentVol
                    }
                  });

                  this.VolumeNSList.push(this.volumeNS);
                  this.user.setVolumeNS(this.VolumeNSList);
                });


                    return {
                      ProvidingStoragePool: currentSPP
                    }
                  });
                });

                return {
                  ProvidingPool: currentPP
                }
            });
          });

            return {
              CapacityPP: currentCSPP
            }
        });
      });

        return {
          Volume: currentVol
        }
      });
    });
    


    //API call to get the subsystem controller names
    this.user.getSubSystemControllers().subscribe( data => {

      console.log("data" + JSON.stringify(data));
      let currentController = "";

      //Array of Controller Names of a Subsystem
      this.controllerDetails = data.Members.map(d => {
        currentController =  d['@odata.id'].split('/')[(d['@odata.id'].split('/').length) -1];
        this.user.setCurrentController(currentController);

        //API call to get an array of volumes and endpoints for a controller in a subsystem
        this.user.getSubSystemControllersDetails().subscribe( dataController => {
          console.log("data" + JSON.stringify(dataController));
          console.log("data links: " + this.controllerDetails)

          //Array of Volumes names for a controller
          this.volumeDetails = dataController.Links.AttachedVolumes.map(dtv => {
            
            //Return for Volume Array
            return {
              Volumes: dtv['@odata.id'].split('/')[(dtv['@odata.id'].split('/').length) -1]
            }
          });
          this.user.setVolumeList(this.volumeDetails);
          
          //Array of Endpoint names for a Controller
          this.endpointDetails = dataController.Links.Endpoints.map(dte => {
            let endpoint = dte['@odata.id'].split('/')[(dte['@odata.id'].split('/').length) -1]
            this.user.setCurrentEndPoint(endpoint);

            //API to get the transport IP details for a specific endpoint
            this.user.getSubSystemEndPointsDetails().subscribe( dataEndP => {
              console.log(dataEndP);

              //Array of transport IP for a specific endpoint
              this.transportIPDetails = dataEndP.IPTransportDetails.map(dti => {
              
              //Return for Transport IP array
              return {
                Protocol: dti['TransportProtocol'],
                IPv4Address: dti['IPv4Address']['Address'],
                Port: dti['Port']
              }
            });
            this.user.setTransportIPList(this.transportIPDetails);
            });

            //Return for Endpoint names array
            return {
              Endpoints: endpoint
            }
          });

          this.user.setEndpointList(this.endpointDetails);

          // //connections read and go into each connection and check if endpoint name exists from the list, here we have current controller name (loop), list of endpoints, list of volumes, list of transport IP
            
          //API call to read connections in the system
          this.user.getSubSystemConnection().subscribe( dataSysConn => {
            let currentConnectionName = "";

            //Array of Connection Names in the system
            this.connectionNameDetails = dataSysConn.Members.map(dconn => {
              currentConnectionName =  dconn['@odata.id'].split('/')[(dconn['@odata.id'].split('/').length) -1];
              this.user.setCurrentConnectionName(currentConnectionName);

              //API call to get TargetEndpoints, Initiator Endpoints and Volume (if TargetEndpoints exists in endpoint list) of a specific connection in the system
              this.user.getSubSystemConnectionDetails().subscribe( dataConn => {
                console.log(dataConn);

                //list of Initiator EP
                let InitiatorEndPointsList = dataConn.Links.InitiatorEndpoints.map(dtep => {
                  
                  //Return of initiator End Point Array in a specific connection
                  return {
                    iEP : dtep['@odata.id'].split('/')[(dtep['@odata.id'].split('/').length) -1] 
                  }                  
                });

                //list of Volume Names
                let VolumeList = dataConn.VolumeInfo.map(dtvi => {

                  //Return of Volume Array in a specific connection
                  return {
                    VolumeEP : dtvi['Volume']['@odata.id'].split('/')[(dtvi['Volume']['@odata.id'].split('/').length) -1] 
                  }                  
                });

                //list of Read/Write
                let accessCapability = dataConn.VolumeInfo.map(dtci => {

                  //Return of Access Capability Array in a specific connection
                  return {
                    accessCP : dtci['AccessCapabilities']
                  }                  
                });

                //Array of TargetEndpoints of a specific connection in the system
                this.connectionDetails = dataConn.Links.TargetEndpoints.map(dtc => {
                  let targetEndP = dtc['@odata.id'].split('/')[(dtc['@odata.id'].split('/').length) -1] 
                  var endPointExists = this.endpointDetails.some((el) => el.Endpoints === targetEndP);
                  if(endPointExists) {
                    //Make a data structure here and continue saving targetEndP, Initiator Endpoints and Volume (ANY if empty)

                    //Link the list of both to the connectionDataList
                    this.connectionDataList.push({targetEP: targetEndP, initiatorEP: InitiatorEndPointsList.length > 0 ? InitiatorEndPointsList : [{iEP: 'ANY'}], volumeInfo: VolumeList, accessCapabilityL : accessCapability})
                    } else {
                      //do nothing
                    }
                    return {
                      TargetEP: targetEndP
                    } 
                  });
                  this.user.setConnectionList(this.connectionDataList);
                });

                return {
                  Connections: currentConnectionName
                }
              });
            });

        });  
    
    //Return for controller names array
    return {
      Controllers: currentController
    }
    });

    this.user.setControllerList(this.controllerDetails);
    });    
  }

  ngOnDestroy() {
    document.body.classList.remove('bg-admin');
  }
}
