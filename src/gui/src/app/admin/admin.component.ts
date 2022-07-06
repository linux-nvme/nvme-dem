import { Component, OnInit } from '@angular/core';
// import { ClientBuilder } from '@iota/client';
import { UserService } from '../user.service';

@Component({
  selector: 'app-admin',
  templateUrl: './admin.component.html',
  styleUrls: ['./admin.component.css']
})
export class AdminComponent implements OnInit {

  message = "Loading.."
  constructor(private user: UserService) { }

  ngOnInit(): void {
    document.body.classList.add('bg-admin');
  }

  ngOnDestroy() {
    document.body.classList.remove('bg-admin');
  }

  upBtnClick() {
    console.log("button clicked");
  // client will connect to testnet by default
  // const client = new ClientBuilder()
  //     .node('https://api.lb-0.h.chrysalis-devnet.iota.cafe:443')    // custom node
  //     .localPow(true)                                         // pow is done locally
  //     .disableNodeSync()                                      // even non-synced node is fine - do not use in production
  //     .build();

  // client.getInfo().then(console.log).catch(console.error);
  }

}
