// import { Injectable } from '@angular/core';
// import { HttpClient, HttpHeaders } from "@angular/common/http";

// interface myData {
//   success: boolean,
//   message: string
// }
// @Injectable({
//   providedIn: 'root'
// })
// export class AuthService {

//  private loggedInStatus = false
//  //private loggedInStatus = JSON.parse(localStorage.getItem('loggedIn') || 'false' )

//   constructor(private http: HttpClient) { }

//   setLoggedIn(value: boolean) {
//     this.loggedInStatus = value
//    // localStorage.setItem('loggedIn', 'true')
//   }

//   get isLoggedIn() {
//     return this.loggedInStatus
//   //  return JSON.parse(localStorage.getItem('loggedIn') || this.loggedInStatus.toString())
//   }
//   getUserDetails(username, password) {
//     const httpOptions = {
//       headers: new HttpHeaders({
//         'Content-Type':  'application/json'
//       })
//     };

//     //post these details to API server return user details if correct
//     return this.http.post <myData>('http://localhost:8080/loginuser',
//     {name:username,password:password},httpOptions);
//   }
// }


import { Injectable } from '@angular/core';
import { HttpClient } from "@angular/common/http";
import {UserService} from './user.service'

interface myData {
  success: boolean,
  message: string
}
@Injectable({
  providedIn: 'root'
})
export class AuthService {

 private loggedInStatus = false
 //private loggedInStatus = JSON.parse(localStorage.getItem('loggedIn') || 'false' )

 userServiceUrl = "";

  constructor(private http: HttpClient, private user: UserService) { 
    this.userServiceUrl = user.url
  }

  setLoggedIn(value: boolean) {
    this.loggedInStatus = value
   // localStorage.setItem('loggedIn', 'true')
  }

  get isLoggedIn() {
    return this.loggedInStatus
  //  return JSON.parse(localStorage.getItem('loggedIn') || this.loggedInStatus.toString())
  }
  getUserDetails(username, password) {
    console.log("Hit");
    //ToDo: IP address and Port number from user input

    return this.http.get(this.userServiceUrl + 'login');
  }
}
