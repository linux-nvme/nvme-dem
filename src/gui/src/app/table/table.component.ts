import { Component, OnInit, Input } from '@angular/core';

@Component({
  selector: 'app-table',
  templateUrl: './table.component.html',
  styleUrls: ['./table.component.css']
})
export class TableComponent implements OnInit {

  constructor() { }
  @Input() dataSource : any
  @Input() openDialog: (args: any) => void;
  @Input() infoImage: string
  @Input() editImage: string
  @Input() deleteImage: string
  @Input() downloadImage: string
  @Input() refreshImage: string
  @Input() columnItems: Object
  @Input() actionColumnRequired: boolean = true;
  @Input() dialogTable: boolean = false;
  @Input() isLicenseScreen: boolean = false;
  @Input() onChangeTableData:(value, element, columnDef)=>void;
  @Input() delete:(element) =>void;

  displayedColumns : string[]=[]
  actionLinkCSS = "action-link";


  ngOnInit(): void {

    if(this.isLicenseScreen == true){
      this.actionLinkCSS = "action-link-license";
    }
    else{
      this.actionLinkCSS = "action-link";
    }

    Object.entries(this.columnItems).forEach(
      ([key, value]) => this.displayedColumns.push(value.columnDef)
    );

    if(this.actionColumnRequired) {
      this.displayedColumns.push('action');
    }

  }

  }
