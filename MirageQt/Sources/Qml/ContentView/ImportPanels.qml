import QtQuick
import QtQuick.Dialogs

Item {
    id: panels
    visible: false
    property string selectedPath: ""
    signal selected(string path)

    FileDialog {
        id: fileDialog
        title: "导入壁纸视频"
        fileMode: FileDialog.OpenFile
        nameFilters: ["视频 (*.mp4 *.mov *.m4v *.webm *.mkv)", "所有文件 (*)"]
        onAccepted: panels.selected(String(selectedFile).replace(/^file:\/\//, ""))
    }
    FolderDialog {
        id: folderDialog
        title: "导入壁纸文件夹"
        onAccepted: panels.selected(String(selectedFolder).replace(/^file:\/\//, ""))
    }

    function openFile() {
        fileDialog.open();
    }
    function openFolder() {
        folderDialog.open();
    }
}
