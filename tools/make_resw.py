#!/usr/bin/env python3
"""Generates src/App/Strings/en-US/Resources.resw from the table below.

Keep English text here; translators get the .resw. "{0}"-style placeholders
are filled in by the app. Keys with a dot are applied by x:Uid in XAML.
"""
import os
from xml.sax.saxutils import escape

APP = "ChatKeeper"  # placeholder name; also in src/App/AppInfo.h

S = {
    # Layout direction for this language (RightToLeft for Arabic).
    "FlowDirection": "LeftToRight",
    "AppName": APP,

    "Step_Key": "Key", "Step_Phone": "Phone", "Step_Copy": "Copy", "Step_Contacts": "Contacts",
    "Step_Unlock": "Unlock", "Step_Build": "Build", "Step_Done": "Done",

    "Nav_Back.Content": "Back", "Nav_Cancel.Content": "Cancel",
    "Next": "Next", "Next_MadeBackup": "I've made the backup", "Next_Unlock": "Unlock",
    "Next_Finish": "Done", "Next_Skip": "Skip",

    "Welcome_Title.Text": "Keep your whole WhatsApp history on this PC",
    "Welcome_Body.Text": "This app copies everything from your Android phone's WhatsApp (messages, photos, videos, voice notes, documents, calls and group members) into one folder on this PC. Open index.html in that folder to read it all in your browser, in a view that looks like WhatsApp. The folder works offline, forever, without the app, WhatsApp, the phone or the phone number.",
    "Welcome_Time.Text": "You'll need your phone, its USB cable and about 15 minutes of your time. Copying can take from a few minutes to an hour, and the folder needs about as much free space as WhatsApp uses on your phone (often several GB).",
    "Welcome_Private.Title": "Everything stays on this computer.",
    "Welcome_Private.Message": "Nothing is uploaded. The app doesn't use the internet at all.",
    "Welcome_Start.Content": "Start",
    "Welcome_AlreadyCopied.Content": "I already copied the WhatsApp folder to this PC",
    "Welcome_NoFolder": "That folder doesn't look like a WhatsApp folder. Choose the folder named “WhatsApp” (it contains Databases and Media).",

    "Guide_Title.Text": "Step 1: Get your key (on the phone)",
    "Guide_Step1.Text": "1. Open WhatsApp → Settings → Chats → Chat backup.",
    "Guide_Step2.Text": "2. Tap End-to-end encrypted backup.",
    "Guide_Step3.Text": "3. Tap Turn on.",
    "Guide_Step4.Text": "4. Choose “Use 64-digit encryption key instead” (not a password).",
    "Guide_Step5.Text": "5. WhatsApp shows a 64-character key. Write it down or take a screenshot and keep it safe. Without it the backup can never be opened, and WhatsApp cannot recover it.",
    "Guide_Step6.Text": "6. Tap Back up and wait until it says the backup is done on the phone.",
    "Guide_NoWait.Title": "You don't need to wait for the upload to Google Drive to finish.",
    "Guide_NoWait.Message": "As soon as WhatsApp starts “Uploading”, the backup is already saved on your phone, and that's the copy this app uses.",
    "Guide_AlreadyOn.Text": "Already have encrypted backup with a 64-digit key turned on? Just tap Back up to make a fresh one.",

    "Phone_Title.Text": "Step 2: Connect your phone",
    "Phone_Instructions.Text": "Plug the phone into this PC with a USB cable and unlock it. When the phone asks “Use USB for…”, choose File transfer. If it asks “Allow access to phone data?”, tap Allow.",
    "Phone_Waiting.Text": "Waiting for your phone…",
    "Phone_Locked.Title": "Your phone is connected but locked or not in File transfer mode.",
    "Phone_Locked.Message": "Unlock it and choose File transfer.",
    "Phone_IsThisYours.Text": "Is this your phone?",
    "Phone_Yes.Content": "Yes, use this phone",
    "Phone_No.Content": "No",
    "Phone_PickOne.Text": "Several phones are connected. Which one is yours?",
    "Phone_UseSelected.Content": "Use this phone",
    "Phone_NotShowing.Header": "Not showing up?",
    "Phone_Tips.Text": "• Unlock the phone and keep the screen on.\n• Pull down the notification shade, tap the USB notification and choose File transfer.\n• Try another cable or another USB port. A charge-only cable won't work.\n• If the phone asks “Allow access to phone data?”, tap Allow.",
    "Phone_NotMine": "Connect your own phone, unlock it and choose File transfer. It will show up here.",
    "Phone_Searching.Text": "Looking for WhatsApp on the phone…",
    "Phone_Found": "WhatsApp folder found ✓",
    "Phone_FoundBusiness": "WhatsApp Business folder found ✓",
    "Phone_BackupToday": "Backup made: today at {0}",
    "Phone_BackupYesterday": "Backup made: yesterday at {0}",
    "Phone_BackupOn": "Backup made: {0}",
    "Phone_Stale": "This backup is from {0}. Make a fresh backup (step 1) so you get your latest messages.",
    "Phone_GoToStep1.Content": "Go back to step 1",
    "Phone_ContinueAnyway.Content": "Continue anyway",
    "Phone_WhichApp.Header": "This phone has both WhatsApp and WhatsApp Business. Which one do you want to keep?",
    "Phone_AppWhatsApp": "WhatsApp",
    "Phone_AppBusiness": "WhatsApp Business",
    "Phone_NoWhatsApp": "WhatsApp wasn't found on this phone. Make sure the phone is unlocked and in File transfer mode, and that WhatsApp is installed on it.",
    "Phone_NoCrypt15": "This phone has no backup with a 64-digit key yet. End-to-end encrypted backup with a 64-digit key is turned off, and older backups can't be opened. Go back to step 1 and turn it on, then make a backup.",
    "Phone_NoBackup": "No WhatsApp backup was found on this phone. Go back to step 1 and make a backup.",
    "Phone_Disconnected": "The phone was disconnected. Plug it in again and unlock it.",
    "Phone_Locked2": "Unlock the phone and choose File transfer, then try again.",

    "Copy_Title.Text": "Step 3: Choose where to save, then copy",
    "Copy_Location.Text": "Save the archive in:",
    "Copy_Change.Content": "Change…",
    "Copy_ChangeLocation.Content": "Change location",
    "Copy_FolderName": "A new folder will be created: {0}",
    "Copy_Start.Content": "Start copying",
    "Copy_Counting": "Counting files on the phone… {0} files so far",
    "Copy_NoSpace": "Not enough space. This needs {0}, and only {1} is free on that drive.",
    "Copy_OneDriveTitle": "This folder is synced to OneDrive",
    "Copy_OneDrive": "This folder is synced to OneDrive. It will upload several GB of photos and videos. Choose a different folder?",
    "Copy_ChooseAnother": "Choose another",
    "Copy_ContinueAnyway": "Continue anyway",
    "Copy_Progress": "{0} of {1} files · {2} of {3}",
    "Copy_Disconnected.Title": "Phone disconnected.",
    "Copy_Disconnected.Message": "Reconnect it to continue. Copying will resume automatically.",
    "Copy_Done": "Copied {0} files ({1}).",
    "Copy_Failed": "{0} files couldn't be copied. The rest of your archive is fine.",
    "Copy_Cancelled": "Copying stopped. What was copied is kept: press Start copying to continue where it left off.",
    "Copy_Error": "Copying failed: {0}",
    "Copy_CannotCreate": "Can't create the folder there. Choose another location.",

    "Contacts_Title.Text": "Step 4: Contact names (optional)",
    "Contacts_Body.Text": "WhatsApp doesn't always store your contacts' names. Add your contacts file so chats show names instead of phone numbers.",
    "Contacts_HowTo.Header": "How do I get a contacts (.vcf) file?",
    "Contacts_TabGoogle.Text": "From Google Contacts (on PC)",
    "Contacts_TabPhone.Text": "From the phone",
    "Contacts_FromGoogle.Text": "1. On this PC, go to contacts.google.com and sign in.\n2. Click Export.\n3. Choose vCard (for iOS Contacts).\n4. Click Export. The file lands in your Downloads folder.",
    "Contacts_FromPhone.Text": "1. On the phone, open the Contacts app.\n2. Open the menu → Manage contacts (or Settings) → Export.\n3. Choose Internal storage.\nThen find the .vcf file on the phone:",
    "Contacts_BrowsePhone.Content": "Find .vcf files on the phone",
    "Contacts_NoPhoneFiles": "No .vcf file was found on the phone. Export your contacts first, or copy the file to this PC.",
    "Contacts_NoPhone": "The phone isn't connected. Use Choose .vcf file instead.",
    "Contacts_Choose.Content": "Choose .vcf file…",
    "Contacts_Skip.Content": "Skip",
    "Contacts_Chosen": "Contacts file: {0}",
    "Contacts_NotVcf": "That file isn't a contacts (.vcf) file. It should start with BEGIN:VCARD.",
    "Contacts_Country.Header": "Your country (for numbers saved without a country code)",
    "Contacts_Country.PlaceholderText": "Country code, e.g. 961",

    "Key_Title.Text": "Step 5: Enter your key",
    "Key_Body.Text": "Type or paste the 64-character key WhatsApp showed you. You can paste it into any box.",
    "Key_Hide.Content": "Hide",
    "Key_ShowText": "Show",
    "Key_HideText": "Hide",
    "Key_Paste.Content": "Paste",
    "Key_Clear.Content": "Clear",
    "Key_Valid": "✓ Key looks right",
    "Key_Count": "{0} of 64 characters",
    "Key_Wrong.Title": "That key doesn't open this backup.",
    "Key_Wrong.Message": "Check for typos and try again. Your copied files are kept.",
    "Key_Privacy.Text": "The key stays in this app's memory only while unlocking. It's never saved, logged or sent anywhere.",

    "Build_Title.Text": "Step 6: Unlock and build",
    "Build_Stage1": "Unlocking backup",
    "Build_Stage2": "Reading messages",
    "Build_Stage3": "Building your archive",
    "Build_Stage4": "Cleaning up",
    "Build_TryAgain.Content": "Try again",
    "Build_BackToKey": "Back to the key",
    "Build_Details.Header": "Technical details",
    "Build_Cancelled": "Stopped. Your copied files are kept; press Try again to start again.",
    "Build_LogSaved": "A log was saved to {0}.",

    "Done_Title.Text": "Done! Your archive is ready",
    "Done_OpenBrowser.Content": "Open in browser",
    "Done_OpenFolder.Content": "Open folder",
    "Done_Advice.Title": "Keep it safe",
    "Done_Advice.Message": "Keep a copy of this folder on another drive or in cloud storage. Also keep your 64-digit key somewhere safe. Everything in this folder already works without it, but you'll need it to unlock the original backup again.",
    "Done_Chats": "{0} chats ({1} groups) · {2} messages",
    "Done_Media": "{0} photos · {1} videos · {2} voice notes and audio · {3} documents · {4} stickers",
    "Done_Calls": "{0} calls",
    "Done_Missing": "{0} media files were no longer on the phone",
    "Done_Size": "Folder size: {0} · {1}",

    "Dialog_Ok": "OK",
    "Dialog_Cancel": "Cancel",
    "Leave_Title": "Stop and close?",
    "Leave_Body": "Copying or unlocking is still running. What was copied so far is kept.",
    "Leave_Stop": "Stop and close",
}


def main():
    out = os.path.join(os.path.dirname(__file__), "..", "src", "App", "Strings", "en-US", "Resources.resw")
    rows = []
    for k, v in S.items():
        rows.append(f'  <data name="{escape(k)}" xml:space="preserve">\n    <value>{escape(v)}</value>\n  </data>')
    xml = f'''<?xml version="1.0" encoding="utf-8"?>
<!-- Generated by tools/make_resw.py. Edit there. -->
<root>
  <xsd:schema id="root" xmlns="" xmlns:xsd="http://www.w3.org/2001/XMLSchema" xmlns:msdata="urn:schemas-microsoft-com:xml-msdata">
    <xsd:element name="root" msdata:IsDataSet="true">
      <xsd:complexType>
        <xsd:choice maxOccurs="unbounded">
          <xsd:element name="data">
            <xsd:complexType>
              <xsd:sequence>
                <xsd:element name="value" type="xsd:string" minOccurs="0" msdata:Ordinal="1" />
                <xsd:element name="comment" type="xsd:string" minOccurs="0" msdata:Ordinal="2" />
              </xsd:sequence>
              <xsd:attribute name="name" type="xsd:string" msdata:Ordinal="1" />
              <xsd:attribute name="type" type="xsd:string" msdata:Ordinal="3" />
              <xsd:attribute name="mimetype" type="xsd:string" msdata:Ordinal="4" />
            </xsd:complexType>
          </xsd:element>
          <xsd:element name="resheader">
            <xsd:complexType>
              <xsd:sequence>
                <xsd:element name="value" type="xsd:string" minOccurs="0" msdata:Ordinal="1" />
              </xsd:sequence>
              <xsd:attribute name="name" type="xsd:string" use="required" />
            </xsd:complexType>
          </xsd:element>
        </xsd:choice>
      </xsd:complexType>
    </xsd:element>
  </xsd:schema>
  <resheader name="resmimetype"><value>text/microsoft-resx</value></resheader>
  <resheader name="version"><value>2.0</value></resheader>
  <resheader name="reader"><value>System.Resources.ResXResourceReader, System.Windows.Forms, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089</value></resheader>
  <resheader name="writer"><value>System.Resources.ResXResourceWriter, System.Windows.Forms, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089</value></resheader>
{chr(10).join(rows)}
</root>
'''
    with open(out, "w", encoding="utf-8", newline="\r\n") as f:
        f.write(xml)


if __name__ == "__main__":
    main()
