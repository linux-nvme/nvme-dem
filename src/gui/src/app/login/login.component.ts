import { Component, OnInit } from '@angular/core';
import { AuthService } from '../auth.service';
import { UserService } from '../user.service';
import {Router} from '@angular/router'
@Component({
  selector: 'app-login',
  templateUrl: './login.component.html',
  styleUrls: ['./login.component.css']
})
export class LoginComponent implements OnInit {

  constructor(private Auth: AuthService, private router: Router, private user: UserService) { }
  data1 : any = [ {
    username: "",
    password: ""
  }]

  ngOnInit(): void {
    document.body.classList.add('bg-img');
  }

  ngOnDestroy() {
    document.body.classList.remove('bg-img');
  }
  
  loginUser(event) {
    event.preventDefault()
    const target = event.target
    const username = target.querySelector('#username').value
    const password = target.querySelector('#password').value
    const ipAddress = target.querySelector('#ipAddress').value
    const port = target.querySelector('#port').value

    console.log(username, password)
    
    this.Auth.getUserDetails(username, password).subscribe(data =>
      {    console.log(data);
            this.data1 = data;
          if(this.data1[0].username == username && this.data1[0].password == password){
            console.log(data)
            this.user.setUserIPAddress(ipAddress);
            this.user.setUserIPPort(port);
            this.router.navigate(['admin'])
            this.Auth.setLoggedIn(true)
          } else {
            window.alert("Invalid credentials");
          }
          //redirect the person to admin

      },
      error =>{
        window.alert("Invalid credentials");
      }
    )
    console.log(event);
  }
}
