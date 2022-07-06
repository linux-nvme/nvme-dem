import { Component, OnInit } from '@angular/core';
import { Subscriber } from 'rxjs';
import {SubSystem} from './../typings'
import { UserService } from '../user.service';
import {Router} from '@angular/router'
import {ModalDismissReasons, NgbModal} from '@ng-bootstrap/ng-bootstrap';
import { FormBuilder, FormGroup, NgForm } from '@angular/forms';


@Component({
  selector: 'app-target-subsystems',
  templateUrl: './target-subsystems.component.html',
  styleUrls: ['./target-subsystems.component.css']
})
export class TargetSubsystemsComponent implements OnInit {

  constructor(private user: UserService, private router: Router, private modalService: NgbModal) { }
  selectedSS : SubSystem;
  targetName : string = "";
  targetTransportIP : string = ""
  headers = [];
  dataHeaders = [];
  rows = [];
  systemNames = [];
  systemStorageNames = [];
  systemVolumeNames = [];
  chassisNames = [];
  networkAdapterNames = [];
  networkDeviceFunctionNames = [];
  ethernetInterfaceNames = [];
  chassisTransportIPList = [];
  closeResult : string = ''
  accessList : any = ['Read', 'Write', 'Read/Write']
  deleteId: any 

  // underlyingVolumes: any = 

  ngOnInit(): void {
    document.body.classList.add('bg-admin');
    this.headers = this.user.getSubSystemTableHeaders();
    this.dataHeaders = this.user.getSubSystemTableDataHeaders();
    this.targetName = this.user.getCurrentTarget();
    this.targetTransportIP = this.user.getCurrentTargetTransportIP();

    this.user.getSubSystemList().subscribe(data => {
      console.log("data" + JSON.stringify(data)  )

      this.rows = data.Members.map(d => {
        return {
          Name: d['@odata.id'].split('/')[(d['@odata.id'].split('/').length) -1]
        }
      });
    });

    //API call to get the system name
    this.user.getSystemName().subscribe(data => {

      //Array of system names
      this.systemNames = data.Members.map(ds => {
        let systemName = ds['@odata.id'].split('/')[(ds['@odata.id'].split('/').length) -1]
        this.user.setCurrentSystemName(systemName);

        //API call to get system storage names
        this.user.getSystemStorageName().subscribe(dss => {

          //Array of system Storage Names 
          this.systemStorageNames = dss.Members.map(dssn => {
            let systemStorageName = dssn['@odata.id'].split('/')[(dssn['@odata.id'].split('/').length) -1]
            this.user.setCurrentSystemStorageName(systemStorageName);

            return {
              Name: systemStorageName
            }
          });

        //API call to get system Volume Names
        this.user.getSystemVolumeName().subscribe(dssv => {
          
          //Array of system Volume Names
          this.systemVolumeNames= dssv.Members.map(dssnv => {
            let systemVolumeName = dssnv['@odata.id'].split('/')[(dssnv['@odata.id'].split('/').length) -1]

            return {
              Name: systemVolumeName
              
            }
          });
        });

        });

        return {
          Name: systemName
        }
      });
    });

    //API call to get Chasis Name
    this.user.getChasisName().subscribe(dch => {

      //Array of chassis names
      this.chassisNames = dch.Members.map(dchs => {
        let chassisName = dchs['@odata.id'].split('/')[(dchs['@odata.id'].split('/').length) -1]
        this.user.setCurrentChassisName(chassisName);

      //API call to get Network Adapter Name
      this.user.getNetworkAdapterName().subscribe(dcna => {

      //Array of network Adapter Names
      this.networkAdapterNames = dcna.Members.map(dcnan => {
        let networkAdaptorName = dcnan['@odata.id'].split('/')[(dcnan['@odata.id'].split('/').length) -1]
        this.user.setCurrentNetworkAdapterName(networkAdaptorName);

      //API call to get Network Device Function
      this.user.getNetworkDeviceFunction().subscribe(dcnf => {

        //Array of network Device Function Names
        this.networkDeviceFunctionNames = dcnf.Members.map(dcnfn => {
          let networkDeviceFunctionName = dcnfn['@odata.id'].split('/')[(dcnfn['@odata.id'].split('/').length) -1]
          this.user.setCurrentNetworkDeviceFunctionName(networkDeviceFunctionName);

          //API call to get Ethernet Interface
          this.user.getEthernetInterfaces().subscribe(dcnei => {

            //Array of network Ethernet Interface Names
            this.ethernetInterfaceNames = dcnei.Members.map(dcnein => {
              let ethernetInterfaceName = dcnein['@odata.id'].split('/')[(dcnein['@odata.id'].split('/').length) -1]
              this.user.setCurrentEthernetInterfaceName(ethernetInterfaceName);

            //API call to get Chasis transport IP details
            this.user.getChassisTransportIPDetails().subscribe(dcnct => {

              //Array of chassis Transport IP Names
              this.chassisTransportIPList = dcnct.IPv4Addresses.map(dcncts => {

                return {
                  Address: dcncts['Address'],
                  SubnetMask: dcncts['SubnetMask'],
                  AddressOrigin: dcncts['AddressOrigin'],
                  Gateway: dcncts['Gateway']
                }
              });
            });                

              return {
                Name: ethernetInterfaceName
              }
            });
          });

          return {
            Name: networkDeviceFunctionName
          }
        });
      });

        return {
          Name: networkAdaptorName
        }
      });
    });

        return {
          Name: chassisName
        }
      });
    });

  }

  ngOnDestroy() {
    document.body.classList.remove('bg-admin');
  }

  onSelect(s_system: SubSystem): void {
    this.selectedSS = s_system;
  }

  infoBtnClicked(row) {
    console.log('Btn Clicked' + row)
    this.router.navigate(['subsystemDetails'])
    this.user.setCurrentSubSystem(row.Name);
  }

  openDelete(targetModal, row) {
    this.deleteId = '2';
    this.modalService.open(targetModal, {
      backdrop: 'static',
      size: 'lg',
      centered: true

    });
  }

  openEdit(targetModal, row) {
  }

  //Dismissal reason for Popup
  private getDismissReason(reason: any): string {
    if (reason === ModalDismissReasons.ESC) {
      return 'by pressing ESC';
    } else if (reason === ModalDismissReasons.BACKDROP_CLICK) {
      return 'by clicking on a backdrop';
    } else {
      return `with: ${reason}`;
    }
  }

    //Add form submit action
    onSubmit(f: NgForm) {
      this.user.addNewTargetServer(f.value)
        .subscribe((result) => {
          this.rows = result.Targets.map(d => {
            return {
              Name: d['Name'],
              Transport_IP: d['IPTransportDetails'][0]['IPv4Address']['Address'] + ':' + d['IPTransportDetails'][0]['Port']
            }
          });
          this.modalService.dismissAll(); //dismiss the modal
        });
    }

    //Add button popup
    open(content) {
      this.modalService.open(content, {ariaLabelledBy: 'modal-basic-title', centered: true
    }).result.then((result) => {
        this.closeResult = `Closed with: ${result}`;
      }, (reason) => {
        this.closeResult = `Dismissed ${this.getDismissReason(reason)}`;
      });
    }
}
