import { async, ComponentFixture, TestBed } from '@angular/core/testing';

import { SubsystemDetailsComponent } from './subsystem-details.component';

describe('SubsystemDetailsComponent', () => {
  let component: SubsystemDetailsComponent;
  let fixture: ComponentFixture<SubsystemDetailsComponent>;

  beforeEach(async(() => {
    TestBed.configureTestingModule({
      declarations: [ SubsystemDetailsComponent ]
    })
    .compileComponents();
  }));

  beforeEach(() => {
    fixture = TestBed.createComponent(SubsystemDetailsComponent);
    component = fixture.componentInstance;
    fixture.detectChanges();
  });

  it('should create', () => {
    expect(component).toBeTruthy();
  });
});
