import { BrowserModule } from '@angular/platform-browser';
import { NgModule } from '@angular/core';
import {RouterModule, Router} from '@angular/router';
import { AppRoutingModule } from './app-routing.module';
import { AppComponent } from './app.component';
import { HttpClientModule} from '@angular/common/http'
import { LoginComponent } from './login/login.component';
import { AdminComponent } from './admin/admin.component';
import { HomeComponent } from './home/home.component';
import { AuthGuard} from './auth.guard'
import {AuthService} from './auth.service'
import { UserService} from './user.service'
import {LogoutComponent} from './logout/logout.component';
import { NavbarIntelComponent } from './navbar_intel/navbar_intel.component';
import { LandingPageComponent } from './landing-page/landing-page.component';
import { ReportingComponent } from './reporting/reporting.component'
// import { TableModule } from 'primeng/table'
// import { TableModule } from 'primeng/table'
import { TableComponent } from './table/table.component';
import { BrowserAnimationsModule } from '@angular/platform-browser/animations';
import {MatTableModule} from '@angular/material/table';
import { TargetSubsystemsComponent } from './target-subsystems/target-subsystems.component';
import { SubsystemDetailsComponent } from './subsystem-details/subsystem-details.component';
import { AccordionComponent } from './accordion/accordion.component'
import { FormsModule, ReactiveFormsModule } from '@angular/forms';
import { NgbModule } from '@ng-bootstrap/ng-bootstrap';

@NgModule({
  declarations: [
    AppComponent,
    LoginComponent,
    AdminComponent,
    HomeComponent,
    LogoutComponent,
    NavbarIntelComponent,
    LandingPageComponent,
    ReportingComponent,
    TableComponent,
    TargetSubsystemsComponent,
    SubsystemDetailsComponent,
    AccordionComponent
  ],
  imports: [
    BrowserModule,
    HttpClientModule,
    // TableModule,
    MatTableModule,
    AppRoutingModule,
    FormsModule,
    ReactiveFormsModule ,
    RouterModule.forRoot([
      {
        path: 'login',
        component: LoginComponent
      },
      {
        path: 'admin',
        component: AdminComponent,
        // canActivate: [AuthGuard]
      },
      {
        path: 'logout',
        component: LogoutComponent
      },

      {
        path: '',
        component: HomeComponent
      },

      {
        path: 'landing',
        component: LandingPageComponent,
        // canActivate: [AuthGuard]
      },

      {
        path: 'reporting',
        component: ReportingComponent,
        // canActivate: [AuthGuard]
      },

      {
        path: 'targetSS',
        component: TargetSubsystemsComponent,
        // canActivate: [AuthGuard]
      }
      ,

      {
        path: 'subsystemDetails',
        component: SubsystemDetailsComponent,
        // canActivate: [AuthGuard]
      }
    ]),
    BrowserAnimationsModule,
    NgbModule,
    // TableModule
  ],
  providers: [AuthService, UserService, AuthGuard],
  bootstrap: [AppComponent]
})
export class AppModule { }
