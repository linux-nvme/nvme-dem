import { Component, OnInit } from '@angular/core';
import { UserService } from '../user.service';
import {Router} from '@angular/router'
import {ModalDismissReasons, NgbModal} from '@ng-bootstrap/ng-bootstrap';
import { FormBuilder, FormGroup, NgForm } from '@angular/forms';

@Component({
  selector: 'app-reporting',
  templateUrl: './reporting.component.html',
  styleUrls: ['./reporting.component.css']
})
export class ReportingComponent implements OnInit {

  message = "Loading.."
  headers = [];
  dataHeaders = [];
  rows = [];
  closeResult : string = ''
  editForm: FormGroup;
  deleteId: any 

  constructor(private user: UserService, private router: Router, private modalService: NgbModal, private fb: FormBuilder) { }

  //Add button popup
  open(content) {
    this.modalService.open(content, {ariaLabelledBy: 'modal-basic-title', centered: true
  }).result.then((result) => {
      this.closeResult = `Closed with: ${result}`;
    }, (reason) => {
      this.closeResult = `Dismissed ${this.getDismissReason(reason)}`;
    });
  }

  //Details popup
  openDetails(targetModal, row) {
    this.modalService.open(targetModal, {
     centered: true,
     backdrop: 'static',
     size: 'lg'
   }).result.then((result) => {
    this.closeResult = `Closed with: ${result}`;
  }, (reason) => {
    this.closeResult = `Dismissed ${this.getDismissReason(reason)}`;
  });

    document.getElementById('targetNameDetails').setAttribute('value', row.Name);
    document.getElementById('targetIPDetails').setAttribute('value', row.Transport_IP);
 }
 
//Edit
 openEdit(targetModal, row) {
  this.modalService.open(targetModal, {
   centered: true,
   backdrop: 'static',
   size: 'lg'
 }).result.then((result) => {
  this.closeResult = `Closed with: ${result}`;
}, (reason) => {
  this.closeResult = `Dismissed ${this.getDismissReason(reason)}`;
});

this.editForm.patchValue( {
  targetNameEdit: row.Name, 
  targetIPEdit: row.Transport_IP
});

  document.getElementById('targetNameDetails').setAttribute('value', row.Name);
  document.getElementById('targetIPDetails').setAttribute('value', row.Transport_IP);
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

  ngOnInit(): void {
    document.body.classList.add('bg-admin');
    this.headers = this.user.getTargetTableHeaders();
    this.dataHeaders = this.user.getTargetTableDataHeaders();

    // this.rows = this.user.getAlarms();
    this.user.getTargetList()
    .subscribe(data => {
      console.log("data" + data  )
      console.log("data" + data.Targets  )
      console.log("data" + data.Targets[0]['Name']  )


      this.rows = data.Targets.map(d => {
        return {
          Name: d['Name'],
          Transport_IP: d['IPTransportDetails'][0]['IPv4Address']['Address'] + ':' + d['IPTransportDetails'][0]['Port']
        }
      });
    });

    this.editForm = this.fb.group({
      targetNameEdit: [''],
      targetIPEdit: ['']
    });
  }

  ngOnDestroy() {
    document.body.classList.remove('bg-admin');
  }

  //Info button details
  infoBtnClicked(row) {
    console.log('Btn Clicked' + row)
    this.router.navigate(['targetSS'])
    this.user.setCurrentTarget(row.Name);
    this.user.setCurrentTargetTransportIP(row.Transport_IP);

  }

  //Save button on Edit form
  onSave() {
    // const editURL = 'http://localhost:8888/friends/' + this.editForm.value.id + '/edit';
    // console.log(this.editForm.value);
    this.user.editTargetServer(this.editForm, '1').subscribe((result) => {
      this.rows = result.Targets.map(d => {
        return {
          Name: d['Name'],
          Transport_IP: d['IPTransportDetails'][0]['IPv4Address']['Address'] + ':' + d['IPTransportDetails'][0]['Port']
        }
      });
        this.modalService.dismissAll();
    });
  }

  openDelete(targetModal, row) {
    this.deleteId = '2';
    this.modalService.open(targetModal, {
      backdrop: 'static',
      size: 'lg',
      centered: true

    });
  }

  onDelete() {
    this.user.deleteTargetServer(this.deleteId).subscribe((result) => {
      this.rows = result.Targets.map(d => {
        return {
          Name: d['Name'],
          Transport_IP: d['IPTransportDetails'][0]['IPv4Address']['Address'] + ':' + d['IPTransportDetails'][0]['Port']
        }
      });
        this.modalService.dismissAll();
    });
  }

}
