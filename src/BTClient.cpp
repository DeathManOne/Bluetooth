#include "../include/BTClient.h"
using namespace Bluetooth;

client::client(uint32_t pinCode) {
    this->_TX = new std::map<std::string, BLERemoteCharacteristic*>();
    this->setCallbacksClient();
    this->setCallbacksSecurity();
    this->setCallbacksNotify();

    BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT_MITM);

    this->_SECURITY = new BLESecurity();
    this->_SECURITY->setKeySize();
    this->_SECURITY->setStaticPIN(pinCode);
    this->_SECURITY->setAuthenticationMode(ESP_LE_AUTH_BOND);
    this->_SECURITY->setCapability(ESP_IO_CAP_NONE);

    this->_CLIENT = BLEDevice::createClient();
}

client::~client() {
    this->disconnect();
    delete this->_CLIENT;
    delete this->_DEVICE;
    delete this->_CHARACTERISTICS;
    delete this->_SECURITY;
    delete this->_TX;
    delete this->_CALLBACKS_CLIENT;
    delete this->_CALLBACKS_NOTIFY;
    delete this->_CALLBACKS_SECURITY;
}

void client::setCallbacksClient(BLEClientCallbacks* callbacks) {
    this->_CALLBACKS_CLIENT = callbacks;
}

void client::setCallbacksSecurity(BLESecurityCallbacks* callbacks) {
    this->_CALLBACKS_SECURITY = callbacks;
}

void client::setCallbacksNotify(notify_callback* callbacks) {
    this->_CALLBACKS_NOTIFY =  callbacks;
}

bool client::isConnected() const {
    return this->_CLIENT->isConnected();
}

bool client::connect(std::string address, int MTU, int scanTime, int connectTimeout) {
    if (this->_DEVICE != nullptr) { this->_DEVICE = nullptr; }
    
    BLEDevice::setSecurityCallbacks(this->_CALLBACKS_SECURITY);
    this->_CLIENT->setClientCallbacks(this->_CALLBACKS_CLIENT);

    Bluetooth::scan *scan = new Bluetooth::scan(scanTime);
    while (this->_DEVICE == nullptr) {
        BLEScanResults devices = scan->start();
        for (int d = 0; d < devices.getCount(); d++) {
            if (devices.getDevice(d).getAddress().toString().compare(address) != 0)
                { continue; }
            this->_DEVICE = new BLEAdvertisedDevice(devices.getDevice(d));
            break;
        }
    }
    scan->stop();
    if (this->_DEVICE == nullptr)
        { return false; }

    bool connected = false;
    const uint32_t timeout = millis() + (1000 * connectTimeout);
    do { connected = this->_CLIENT->connect(this->_DEVICE); }
    while (!connected && static_cast<uint32_t>(millis()) < timeout);

    if (!this->_CLIENT->isConnected())
        { return false; }
    this->_CLIENT->setMTU(MTU);
    this->characteristics();
    return true;
}

bool client::disconnect() {
    if (!this->_CLIENT->isConnected()) { return true; }
    this->_CLIENT->disconnect();
    this->_DEVICE = nullptr;
    this->_CHARACTERISTICS = nullptr;
    this->_TX->clear();
    return !this->_CLIENT->isConnected();
}

void client::characteristics() {
    if (this->_CHARACTERISTICS != nullptr)
        { this->_CHARACTERISTICS = nullptr; }
    this->_TX->clear();
    if (!this->_DEVICE->haveServiceUUID())
        { return; }

    BLERemoteService *remote = this->_CLIENT->getService(this->_DEVICE->getServiceUUID());
    this->_CHARACTERISTICS = remote->getCharacteristics();
    for (auto c = this->_CHARACTERISTICS->begin(); c != this->_CHARACTERISTICS->end() ; ++c) {
        if (c->second->canRead())
            {}
        if (c->second->canBroadcast())
            {}
        if (c->second->canIndicate())
            {}
        if (c->second->canNotify())
            { c->second->registerForNotify(*this->_CALLBACKS_NOTIFY); }
        if (c->second->canWrite())
            { this->addTx(c->first, c->second); }
        if (c->second->canWriteNoResponse())
            { this->addTx(c->first, c->second); }
    }
}

void client::addTx(std::string uuid, BLERemoteCharacteristic *characteristic) {
    for (auto t = this->_TX->begin(); t != this->_TX->end(); ++t) {
        if (t->first.compare(uuid) == 0)
            { return; }
    }
    this->_TX->insert({uuid, characteristic});
}

void client::sendCommand(std::string uuid, std::string data) const {
    for (auto t = this->_TX->begin(); t != this->_TX->end(); ++t) {
        if (!t->second->getUUID().equals(BLEUUID(uuid)))
            { continue; }
        t->second->writeValue(data, t->second->canWrite());

        delay(250);
        break;
    }
}