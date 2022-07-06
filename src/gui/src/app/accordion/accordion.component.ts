import { Component, OnInit } from '@angular/core';
import { UserService } from '../user.service';

@Component({
  selector: 'app-accordion',
  templateUrl: './accordion.component.html',
  styleUrls: ['./accordion.component.css']
})
export class AccordionComponent implements OnInit {

  constructor(private user: UserService) { }
  acc1Visible : boolean = false
  acc2Visible : boolean = false
  acc3Visible : boolean = false
  acc4Visible : boolean = false
  acc5Visible : boolean = false

  acc1Content = this.user.getControllerList()
  acc2Content = this.user.getVolumeList()
  acc2SubContent = this.user.getVolumeNS()
  acc3Content = this.user.getEndpointList()
  acc4Content = this.user.getTransportIPList()
  acc5Content = this.user.getConnectionList()

  ngOnInit(): void {
  }

  id: any = ''

  onClick(param: any) {
    this.acc1Content = this.user.getControllerList()
    this.acc2Content = this.user.getVolumeList()
    this.acc2SubContent = this.user.getVolumeNS()
    this.acc3Content = this.user.getEndpointList()
    this.acc4Content = this.user.getTransportIPList()
    this.acc5Content = this.user.getConnectionList()

    console.log(param.target.id);
    if(this.id == param.target.id) {
      this.id = '';
    } else {
      this.id = param.target.id;
    }

    switch(param.target.id) {
      case 'f1': this.acc1Visible = !this.acc1Visible;
      break;
      case 'f2': this.acc2Visible = !this.acc2Visible;
      break;      
      case 'f3': this.acc3Visible = !this.acc3Visible;
      break;
      case 'f4': this.acc4Visible = !this.acc4Visible;
      break;
      case 'f5': this.acc5Visible = !this.acc5Visible;
      break;
    }
  }

}
