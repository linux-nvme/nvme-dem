import { async, ComponentFixture, TestBed } from '@angular/core/testing';

import { NavbarIntelComponent } from './navbar_intel.component';

describe('NavbarIntelComponent', () => {
  let component: NavbarIntelComponent;
  let fixture: ComponentFixture<NavbarIntelComponent>;

  beforeEach(async(() => {
    TestBed.configureTestingModule({
      declarations: [ NavbarIntelComponent ]
    })
    .compileComponents();
  }));

  beforeEach(() => {
    fixture = TestBed.createComponent(NavbarIntelComponent);
    component = fixture.componentInstance;
    fixture.detectChanges();
  });

  it('should create', () => {
    expect(component).toBeTruthy();
  });
});
