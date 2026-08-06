import QtQuick

QtObject {
    property int currentStep: 0
    property string username: ""
    property string password: ""
    property string guardCode: ""
    property bool canProceed: currentStep === 0 || currentStep === 3
}
