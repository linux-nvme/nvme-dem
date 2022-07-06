import { async, ComponentFixture, TestBed } from '@angular/core/testing';

import { TargetSubsystemsComponent } from './target-subsystems.component';

describe('TargetSubsystemsComponent', () => {
  let component: TargetSubsystemsComponent;
  let fixture: ComponentFixture<TargetSubsystemsComponent>;

  beforeEach(async(() => {
    TestBed.configureTestingModule({
      declarations: [ TargetSubsystemsComponent ]
    })
    .compileComponents();
  }));

  beforeEach(() => {
    fixture = TestBed.createComponent(TargetSubsystemsComponent);
    component = fixture.componentInstance;
    fixture.detectChanges();
  });

  it('should create', () => {
    expect(component).toBeTruthy();
  });
});
