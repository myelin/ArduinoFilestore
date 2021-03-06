
void rxFrame(){
  int ptr=0;
  byte stat=0;
  boolean frame=true;
  static boolean gotScout=false; // This maintains our state in the 4 way handshake between calls.
  static unsigned long scoutTimeout=0;
  

  // First byte should be address
  rxBuff[0]=readFIFO();

  if (rxBuff[0] != MYSTATION && rxBuff[0] != 255 ) {
      // If frame is not for me, or a broadcast, then bail out
      resetIRQ();
      return;
  }
  
  ptr++;

  if (millis()>scoutTimeout && gotScout==true) { gotScout=false; Serial.println("Scout timeout");} ;

  while (frame){     
   // Now check the status register
 
    do {
      delayMicroseconds(1); // Even at the fastest clock rate, it still takes several uS to get another byte.      
      stat=readSR2();
    } while (!(stat & 250)); // While no errors, or available data - keep polling  

    rxBuff[ptr]=readFIFO();
    if (ptr < BUFFSIZE) {ptr++;}; //TODO: keep overwriting last byte for now until I do this properly and abort the rx!

    if (stat & 122 ) frame=false; // If error or end of frame bits set, drop out
  } // End of while in frame - Data Available with no error bits or end set
 

  if (stat &2 ){ 
    // Frame is valid
  
    if (rxBuff[0]==255 || rxBuff[0]==0){
      //Broadcast frame - treat specially, then drop out of receive flow
      rxBroadcast(ptr);        
      return;
    };

    // Still here so a frame addressed to me - flag fill while we work out what to do with it
    
    flagFill(); // Flag fill and reset statuses - seems to need calling twice to clear everything
    flagFill();
    
    if (gotScout==false){
    
      // Am expecting a scout here, but could be an immediate op
      if (ptr>7){
        // Too big for a scout so must be an immediate operation
        doImmediateOpRX(ptr);      
      } else {
        // Acknowledge the scout, and set flag for next run
        
        // TODO: I really only should acknowledge if I'm expecting a frame by checking these....  
        // rxBuff[3] = Sender station
        // rxBuff[4] = Sender network
        // rxBuff[5] = ControlByte
        // rxBuff[6] = Port
        
        // ... but I'm not for the moment!
  
        // Make a note of these, as we won't get them again in the payload
        rxControlByte=rxBuff[4];
        rxPort=rxBuff[5];
      
        ackRX();
        gotScout=true;
        scoutTimeout=millis()+SCOUTTIMEOUT;
      }
     
    } else {

        // Have got a payload after the scout, acknowledge and process
        ackRX();
        gotScout=false;
        if (rxPort==0x99) { fsOperation(ptr); } else { processFrame(ptr); };

    }
    
    // Do a final clear of any outstanding status bits
    delayMicroseconds(1);  
    rxReset();

  } else {
    // Frame not valid - what happened?

    Serial.print("RxFrame ended due to error: ");
    printSR2(stat);
    Serial.println("");

    displayBuffer(ptr);

    // Reset the RXStatus bit
    delayMicroseconds(1);  
    rxReset();
    gotScout=false;
  
    //Recheck status
    delayMicroseconds(2);
    Serial.print ("SR1 =");
    stat=readSR1();
    printSR1(stat);
    delayMicroseconds(1);
    Serial.print ("  SR2 =");
    stat=readSR2();
    printSR2(stat);
    Serial.println ("");
  }  
}

void processFrame (int bytes){
  // Not an FS command, but could be a bulk data transfer
  
  if (fHandleActive[rxPort-129]) {
     fsBulkRXArrived(rxPort, bytes);
     return;  
  }
  
  Serial.print ("Unexpected frame of ");
  Serial.print (bytes);  
  Serial.print (" bytes from ");
  Serial.print (rxBuff[3]);
  Serial.print (".");
  Serial.print (rxBuff[2]);
  Serial.print (", control byte = ");
  Serial.print (rxControlByte,HEX);
  Serial.print (", port = ");
  Serial.print (rxPort,HEX);
  Serial.println("."); 

  for (int ptr1=4; ptr1 < bytes ; ptr1++){
    Serial.print (rxBuff[ptr1],HEX);
    Serial.print (" ");
  }
  Serial.print ("= ");
  for (int ptr1=4; ptr1 < bytes ; ptr1++){
    if (rxBuff[ptr1]<32 || rxBuff[ptr1]> 126){
      Serial.print (".");
    } else {
      Serial.write (rxBuff[ptr1]);
    }
  } 
  Serial.println (" ");
}

void displayBuffer(int lastbyte){
    Serial.print (lastbyte);
    Serial.print (" bytes in buffer");
    Serial.println (); 
    for (int ptr1=0; ptr1 < lastbyte ; ptr1++){
      Serial.print (rxBuff[ptr1],HEX);
      Serial.print (" ");
    } 
    Serial.println (" ");
}

void ackRX(){
  // Generate an acknowledgement packet 
  
  // Build the ack frame from the data in rx buffer  
  txBuff[0]=rxBuff[2]; 
  txBuff[1]=rxBuff[3];
  txBuff[2]=rxBuff[0];
  txBuff[3]=rxBuff[1];
  
  txFrame(4);  // And transmit it

  return;
}

void rxBroadcast (int bytes){
  int controlByte=rxBuff[4];
  int rxPort=rxBuff[5];
  printTime();
  Serial.print (" Received broadcast of ");
  Serial.print (bytes-6);
  Serial.print (" bytes from ");
  Serial.print (rxBuff[3]);
  Serial.print (".");
  Serial.print (rxBuff[2]);
  Serial.print (", control byte = ");
  Serial.print (controlByte,HEX);
  Serial.print (", port = ");
  Serial.print (rxPort,HEX);
  Serial.print(", bytes: "); 
  for (int ptr1=6; ptr1 < bytes ; ptr1++){
    Serial.print (rxBuff[ptr1],HEX);
    Serial.print (" ");
  }
  Serial.print ("= ");
  for (int ptr1=6; ptr1 < bytes ; ptr1++){
    if (rxBuff[ptr1]<32 || rxBuff[ptr1]> 126){
      Serial.print (".");
    } else {
      Serial.write (rxBuff[ptr1]);
    }
    Serial.print (" ");
  } 
  Serial.println (" ");

  if (controlByte==0x80 && rxPort==0x99){
    // This is a server command broadcast - probably a ListFS
    printTime();
    Serial.print (" Sending ListFS response ");
    if (txBuff[7]=14) fsReadDiskInfo(rxBuff[6]);
  }
}

void doImmediateOpRX(int rxSize){
  // Perform the operation and generate an acknowledgement packet if valid

  byte opCode=rxBuff[4];

  printTime();
  Serial.print (" Immediate operation from ");
  Serial.print (rxBuff[3]);
  Serial.print (".");
  Serial.print (rxBuff[2]);
  Serial.print (", operation type  = ");
  Serial.print (opCode,HEX);

  // Now work out what has actually been requested!
  
  switch (opCode){
    case 0x81:
      // Peek
      Serial.println (" (Peek) - Ignoring!");
      // Don't acknowledge
      break;
      
    case 0x82:
      // Poke
      Serial.println (" (Poke) - Ignoring!");
      // Don't acknowledge
      break;

    case 0x83:
      // JSR
      Serial.println (" (JSR) - Ignoring!");
      // Don't acknowledge
      break;

    case 0x84:
      // User procedure call
      Serial.println (" (User procedure call) - Ignoring!");
      // Don't acknowledge
      break;

    case 0x85:
      // OS procedure call
      Serial.println (" (OS procedure call) - Ignoring!");
      // Don't acknowledge
      break;

    case 0x86:
      // Halt
      Serial.println (" (Halt) - Ignoring!");
      // Don't acknowledge
      break;

    case 0x87:
      // Continue
      Serial.println (" (Continue) - Ignoring!");
      // Don't acknowledge
      break;

    case 0x88:
      // Machine peek
      Serial.print (" (Machine peek)");

      // Build a response
      txBuff[0]=rxBuff[2]; 
      txBuff[1]=rxBuff[3];
      txBuff[2]=rxBuff[0];
      txBuff[3]=rxBuff[1];
/*
      txBuff[4]=0x01; // I am a BBC Micro (really!)
      txBuff[5]=0x00;
      txBuff[6]=0x3C; // Running NFS 3.60
      txBuff[7]=0x03;
      Serial.print (" - Replying BBC Micro NFS 3.60.....");
*/
      txBuff[4]=0x0B;
      txBuff[5]=0x00;
      txBuff[6]=0x02;
      txBuff[7]=0x00;
      Serial.print (" - Replying Filestore V2.00 ");
      
      if (txFrame(8)) { Serial.println ("Done!"); } else { Serial.println ("Failed!"); };
      
      break;
      
    default:
      Serial.println (" (Unknown) ");
  }
    
  return;
}

boolean txFrame(int bytes){
  int sr1,sr2;
  unsigned long timeOut;
  
  writeCR1(B00000000); // Disable RX interrupts, select address 1

  timeOut=millis()+TXTIMEOUT;
  sr1=readSR1();
  
  while(!(sr1 & 64)){ // If we don't have TDRA, clear status until we do!  
    writeCR2(B11100101); // Raise RTS, clear TX and RX status, flag fill and Prioritise status
    delayMicroseconds(1);
    sr1=readSR1();
  }

  digitalWriteDirect(PIN_LED,1);

  delayMicroseconds(10); // Give the other clients a moment to notice the flag fill 
 
  for(int buffPtr=0;buffPtr<bytes;buffPtr+=1){
    sr1=readSR1();
    if (sr1 & 2) sr2=readSR2();    
    while(true){ // While not TDRA set, loop until it is - or we get an error
      if (sr1 & 64) break;
      if (sr1 & 32) {Serial.print ("TX Underrun at "); Serial.println (buffPtr); resetIRQ(); return(false);};
      if (!sr1 & 16) {Serial.println ("CTS loss"); resetIRQ(); return(false);};
      //delayMicroseconds(1);
      if (millis()>timeOut){Serial.print("TX timeout on byte "); Serial.print(buffPtr); resetIRQ(); return(false);}
      sr1=readSR1();
      if (sr1 & 2) sr2=readSR2();       
    }

  digitalWriteDirect(PIN_LED,0);
  
    // Now we are ready, write the byte.
    writeFIFO(txBuff[buffPtr]);
  } // End of for loop to tx bytes

  writeCR2(B00010001); // Tell the ADLC that was the last byte, and clear flag fill modes and RTS.
  
  // Do a last check for errors
  sr1=readSR1();

  if ((sr1 & 2) || (sr1 & 32) || (!sr1 &16)){
    if (sr1 & 2) sr2=readSR2();    
    if (sr1 & 32){ 
      //TX underrun
      Serial.println("TX underrun");
      initADLC(); //reset the ADLC
      return (false);
    }
    if (!sr1 & 16){ 
      //CTS loss
      Serial.println("CTS loss");
      initADLC(); //reset the ADLC
      return (false);
    }          
  }
  writeCR1(B00000010); //Enable RX interrupts, select address 1
  return (true);
}

boolean txScoutFrame(){
  int sr1,sr2;
  sr1=readSR1();
//  digitalWriteDirect(PIN_LED,1);
  while(!(sr1 & 64)){ // If we don't have TDRA, clear status until we do!  
    writeCR2(B11100101); // Raise RTS, clear TX and RX status, flag fill and Prioritise status
    delayMicroseconds(1);
    sr1=readSR1();
  }
//  digitalWriteDirect(PIN_LED,0);
  
  for(int buffPtr=0;buffPtr!=6;buffPtr+=1){
    sr1=readSR1();
    if (sr1 & 2) sr2=readSR2();    
    while(true){ // While not TDRA set, loop until it is - or we get an error
      if (sr1 & 64) break;
      if (sr1 & 32) {Serial.print ("TX Underrun at "); Serial.println (buffPtr); return(false);};
      if (!sr1 & 16) {Serial.println ("CTS loss"); return(false);};
      //delayMicroseconds(1);
      sr1=readSR1();
      if (sr1 & 2) sr2=readSR2();       
    }


    // Now we are ready, write the byte.
    writeFIFO(scoutBuff[buffPtr]);
  } // End of for loop to tx bytes

  writeCR2(B00010001); // Tell the ADLC that was the last byte, and clear flag fill modes and RTS.
  
  // Do a last check for errors
  sr1=readSR1();

  if ((sr1 & 2) || (sr1 & 32) || (!sr1 &16)){
    if (sr1 & 2) sr2=readSR2();    
    if (sr1 & 32){ 
      //TX underrun
      Serial.println("TX underrun");
      initADLC(); //reset the ADLC
      return (false);
    }
    if (!sr1 & 16){ 
      //CTS loss
      Serial.println("CTS loss");
      initADLC(); //reset the ADLC
      return (false);
    }          
  }
  return (true);
}

boolean waitIdle(){
  //Wait for network to become idle, returns false if network error or if not idel in 5 seconds.
  unsigned int byte1;
  int sr1,sr2; 

//  digitalWriteDirect(PIN_LED,1);
  
  unsigned long timeOut=millis()+2000;


  sr2=0; // Force while loop to run first time
  while (!(sr2 & 4)){
    delay(50);
    sr1=readSR1();
    sr2=readSR2();

    if (sr2 & 32){ //DCD loss (no clock)
      Serial.print("Error: ");
      printSR1(sr2);
      Serial.println();
      return (false);
    }
    
    if (millis()>timeOut){
      Serial.println("Network not idle for 2 seconds");
      return(false);
    }

    resetIRQ();

  }
//  digitalWriteDirect(PIN_LED,0);
  
  return(true);
  
}

boolean txWithHandshake(int lastByte, int port, int controlByte){
  int attempt=0;
  while (attempt<TXRETRIES){
    if (txWithHandshakeInner(lastByte, port, controlByte)) return(true);
    attempt++;
    //Serial.print(" txretry ");
    delay(TXRETRYDELAY);
  }
  return(false);
}

boolean txWithHandshakeInner(int lastByte, int port, int controlByte){
  int sr1,sr2;
  //First generate the scout
  
  scoutBuff[0]=txBuff[0];
  scoutBuff[1]=txBuff[1];
  scoutBuff[2]=txBuff[2];
  scoutBuff[3]=txBuff[3];
  scoutBuff[4]=controlByte;
  scoutBuff[5]=port;
  digitalWriteDirect(PIN_LED,0);
  // Wait for network to become idle, or bail out if network error
  if (!waitIdle()) return(false);

  if (!txScoutFrame()) {resetIRQ(); return(false);};


  // Scout sent, listen for ack
  if (!waitForAck()) {return(false);};
  
  digitalWriteDirect(PIN_LED,0);
  // Now send the payload
  if (!txFrame(lastByte)) {resetIRQ(); return(false);};
 
  // And wait for acknowledgement
  if (!waitForAck()) {return(false);};

//  digitalWriteDirect(PIN_LED,0);
//  resetIRQ(); // Just in case we bailed out somewhere leaving interrupts disabled
  
  return(true);
}

boolean waitForAck(){
  byte statReg1, statReg2;
  boolean ackResult=false, inLoop=true;
 
  unsigned long timeOut=millis()+1;

  resetIRQ();

  while (inLoop){ // Enter IRQ polling loop
    if (millis()>timeOut) inLoop=false;

    if (!digitalReadDirect(PIN_IRQ)){
      //There is an IRQ to service
      statReg1=readSR1();
      
      if (statReg1 & 2){
        statReg2=readSR2();

        if (statReg2 & 1) { ackResult=checkAck();};     // Address present in FIFO, so fetch it and the rest of frame
        if (statReg2 & 2 ) { readFIFO(); resetIRQ(); }; // Frame complete - not expecting a frame here
        if (statReg2 & 4 ) { resetIRQ(); };             // Inactive idle 
        if (statReg2 & 8 ) { resetIRQ(); };             // TX abort received 
        if (statReg2 & 16 ) { resetIRQ(); };            // Frame error 
        if (statReg2 & 32) { Serial.println("No clock!"); resetIRQ(); }; // Carrier loss
        if (statReg2 & 64) { resetIRQ(); };             // Overrun  
        if (statReg2 & 128) { readFIFO(); };            // RX data available 
      } else {
        //Something else in SR1 needs servicing        
        if (statReg1 & 1) { readFIFO(); }; // Not expecting data, so just read and ignore it!
        resetIRQ(); //Reset IRQ as not expecting anything!
      }// end of if SR2 needs servicing
    } // end of IF IRQ to service
    
    if (ackResult) inLoop=false;
   
  } // end of event loop


  return(ackResult);
}

boolean checkAck(){

  byte theirStation=scoutBuff[2];
  byte theirNet=scoutBuff[3];
  
  int ptr=0;
  byte stat=0;
  boolean frame=true;
  static boolean gotScout=false; // This maintains our state in the 4 way handshake between calls.

  // First byte should be address
  rxBuff[0]=readFIFO();

  if (rxBuff[0] != MYSTATION) {
      // If frame is not for me, then bail out
      resetIRQ();
      return(false);
  }
  
  ptr++;

  while (frame){
    
    // Now check the status register
    do {
      delayMicroseconds(1); // Even at the fastest clock rate, it still takes several uS to get another byte.      
      stat=readSR2();
    } while (!(stat & 250)); // While no errors, or available data - keep polling  

    rxBuff[ptr]=readFIFO();
    if (ptr < BUFFSIZE) {ptr++;}; //keep overwriting last byte for now until I do this properly!


    if (stat & 122 ) frame=false; // If error or end of frame bits set, drop out
  } // End of while in frame - Data Available with no error bits or end set
 

   // Frame is valid
   if(ptr!=4) return(false); // frame is wrong size for an ack
   if((rxBuff[1]==theirNet) && (rxBuff[0]==theirStation)) return (true);
 
  return (false);   // Not bothering with any other error checking - if we are here the frame is wrong regardless of cause.
}

void flagFill(){
  writeCR2(B11100100); // Set CR2 to RTS, TX Status Clear, RX Status clear, Flag fill on idle)
}

void rxReset(){
  delayMicroseconds(2); // Give the last byte a moment to drain
  writeCR2(B00100001); // Clear RX status, prioritise status 
}

void listFS(){
  txBuff[0]=0xFF; 
  txBuff[1]=0xFF;
  txBuff[2]=MYSTATION;
  txBuff[3]=0;
  txBuff[4]=0x80;
  txBuff[5]=0x99;
  txBuff[6]=0x06;
  txBuff[7]=0x0E;
  txBuff[8]=0x00;
  txBuff[9]=0x00;
  txBuff[10]=0x00;
  txBuff[11]=0x00;
  txBuff[12]=0x04;
  txBuff[13]=0x00;

  if (!txFrame(14)){ Serial.println(F("Failed to send ListFS"));};
}

void bridgeProbe(){
  txBuff[0]=0xFF; 
  txBuff[1]=0xFF;
  txBuff[2]=MYSTATION;
  txBuff[3]=0;
  txBuff[4]=0x82;
  txBuff[5]=0x9C;
  txBuff[6]=0x42;
  txBuff[7]=0x72;
  txBuff[8]=0x69;
  txBuff[9]=0x64;
  txBuff[10]=0x67;
  txBuff[11]=0x65;
  txBuff[12]=0x9C;
  txBuff[13]=0x00;

  if (!txFrame(14)){ Serial.println(F("Failed to send bridge discovery broadcast"));};
}

