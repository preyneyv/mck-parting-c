interface USBEndpoint { endpointNumber: number; direction: "in" | "out"; }
interface USBAlternateInterface { interfaceClass: number; endpoints: USBEndpoint[]; }
interface USBInterface { interfaceNumber: number; alternates: USBAlternateInterface[]; alternate: USBAlternateInterface; }
interface USBConfiguration { configurationValue: number; interfaces: USBInterface[]; }
interface USBInTransferResult { data?: DataView; status: string; }
interface USBOutTransferResult { bytesWritten?: number; status: string; }
interface USBDevice {
  vendorId: number; productId: number; serialNumber?: string; opened: boolean;
  configuration?: USBConfiguration;
  configurations: USBConfiguration[];
  open(): Promise<void>;
  close(): Promise<void>;
  selectConfiguration(value: number): Promise<void>;
  claimInterface(interfaceNumber: number): Promise<void>;
  releaseInterface(interfaceNumber: number): Promise<void>;
  clearHalt(direction: "in" | "out", endpointNumber: number): Promise<void>;
  transferIn(endpointNumber: number, length: number): Promise<USBInTransferResult>;
  transferOut(endpointNumber: number, data: BufferSource): Promise<USBOutTransferResult>;
}
interface USBConnectionEvent extends Event { device: USBDevice; }
interface USB {
  requestDevice(options: { filters: Array<{ vendorId?: number; productId?: number }> }): Promise<USBDevice>;
  getDevices(): Promise<USBDevice[]>;
  addEventListener(type: "disconnect", listener: (event: USBConnectionEvent) => void): void;
  removeEventListener(type: "disconnect", listener: (event: USBConnectionEvent) => void): void;
}
interface Navigator { usb: USB; }
