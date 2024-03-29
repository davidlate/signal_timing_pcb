const int RELAYPIN_1 = 10;
const int RELAYPIN_2 = 6;
const int TENS_READ_PIN = 2;
int x = 1;
int  Y= 1;
const float startTime = millis();
float lastTime;
int num_changes;
float time = millis();
float delta;
float deBounce = 10; //min interval between readings in milliseconds.

void setup() {
  Serial.begin(9600);

  pinMode(RELAYPIN_1, OUTPUT);
  pinMode(RELAYPIN_2, OUTPUT);
  pinMode(TENS_READ_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(TENS_READ_PIN), read_changes, CHANGE);
}

void loop() {
  Serial.print(Y);
  Serial.print("\n");
  Serial.print("working...\n");
  Y++;
  delay(1000);
}

void read_changes() {
  delta = millis() - lastTime;
  if (delta < deBounce) {
    return 0;
  }
  detachInterrupt(digitalPinToInterrupt(TENS_READ_PIN));
  Serial.print("CHANGE \n");

  Serial.print(x);
  Serial.print("\n");
  Serial.print(millis());
  Serial.print("\n");

  x++;
  lastTime = millis();
  attachInterrupt(digitalPinToInterrupt(TENS_READ_PIN), read_changes, CHANGE);
  
}