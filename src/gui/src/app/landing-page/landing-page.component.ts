import { Component, OnInit } from '@angular/core';
import { UserService } from '../user.service';
import {Hosts} from '../interface';

@Component({
  selector: 'app-landing-page',
  templateUrl: './landing-page.component.html',
  styleUrls: ['./landing-page.component.css']
})

export class LandingPageComponent implements OnInit {

  message = "Loading.."
  data: any;
  headers = [];
  dataHeaders = [];
  rows = [];

  // data: Hosts[];
  constructor(private user: UserService) { 
    this.user.getHostList().subscribe(data => {
      this.data = data;
    })
  }

  ngOnInit(): void {
    document.body.classList.add('bg-admin');
    this.headers = this.user.getHostTableHeaders();
    this.dataHeaders = this.user.getHostTableDataHeaders();

    // this.rows = this.user.getAlarms();
    this.user.getHostList()
    .subscribe(data => {
      this.rows = data.Members.map(d => {
        return {
          Name: d['@odata.id'].split('/')[(d['@odata.id'].split('/').length) -1]
        }
      });
    });
  }

  ngOnDestroy() {
    document.body.classList.remove('bg-admin');
  }

  //Info button details
  infoBtnClicked(row) {
    console.log('Btn Clicked' + row)
    // this.router.navigate(['targetSS'])
  }

  openDelete(targetModal, row) {
  }

  openEdit(targetModal, row) {
  }

 my_button_click_handler(event)
  {
    alert('Container information submitted to IOTA Tangle');
  }
}
