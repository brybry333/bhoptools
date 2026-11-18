// Options > Keyboard lists the actions in scripts/kb_act.lst, and its "Use Defaults" binds what kb_def.lst says. The
// game reads the first copy on its search path: cstrike/custom/* come first (alphabetical, only folders there at boot),
// so ours goes in custom/bhoptools before the game starts: a bhoptools section, then CS:S's own list (11003710's,
// unchanged). CS:TV, the Progress Log and paint have no default key, so they never take one of the player's.
#pragma once

#include <cstdio>
#include <string>
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

namespace keylists {

const char kStockActions[] = R"KB("blank"		"=========================="
"blank"		"#Valve_Movement_Title"
"blank"		"=========================="
"+forward"		"#Valve_Move_Forward"
"+back"			"#Valve_Move_Back"
"+moveleft"		"#Valve_Move_Left"
"+moveright"	"#Valve_Move_Right"
"+speed"       	"#Valve_Walk"
"+jump"			"#Valve_Jump"
"+duck"			"#Valve_Duck"
"blank"		"=========================="
"blank"		"#Cstrike_Combat_Title"
"blank"		"=========================="
"+attack"		"#Cstrike_Fire"
"+attack2"		"#Cstrike_Weapon_Special_Func"
"+reload"		"#Valve_Reload_Weapon"
"invprev"		"#Valve_Previous_Weapon"
"invnext"		"#Valve_Next_Weapon"
"lastinv"		"#Valve_Last_Weapon_Used"
"drop"	 		"#Cstrike_Drop_Weapon"
"blank"		"=========================="
"blank"		"#Valve_Communication_Title"
"blank"		"=========================="
"+voicerecord"	"#Valve_Use_Voice_Communication"
"radio1"		"#Cstrike_Standard_Radio"
"radio2"		"#Cstrike_Group_Radio"
"radio3"		"#Cstrike_Report_Radio"
"messagemode"	"#Valve_Chat_Message"
"messagemode2"	"#Valve_Team_Message"
"blank"		"=========================="
"blank"		"#Cstrike_Menu_Title"
"blank"		"=========================="
"buymenu" 		"#Cstrike_Buy_Menu"
"buyequip"		"#Cstrike_Buy_Equip_Menu"
"autobuy"		"#Cstrike_Auto_Buy"
"rebuy"			"#Cstrike_ReBuy"
"chooseteam"	"#Cstrike_Select_Team"
"+showscores"	"#Valve_Display_Scores"
"showbriefing"	"#Cstrike_Mission_Briefing"
"slot10"		"#Cstrike_Menu_Item_0"
"slot1"			"#Cstrike_Menu_Item_1"
"slot2"			"#Cstrike_Menu_Item_2"
"slot3" 		"#Cstrike_Menu_Item_3"
"slot4"			"#Cstrike_Menu_Item_4"
"slot5"			"#Cstrike_Menu_Item_5"
"slot6"			"#Cstrike_Menu_Item_6"
"slot7"			"#Cstrike_Menu_Item_7"
"slot8"			"#Cstrike_Menu_Item_8"
"slot9"			"#Cstrike_Menu_Item_9"
"blank"		"=========================="
"blank"		"#Valve_Miscellaneous_Title"
"blank"		"=========================="
"+use"			"#Valve_Use_Items"
"impulse 100"	"#Valve_Flashlight"
"nightvision"	"#Cstrike_Nightvision"
"impulse 201"	"#Valve_Spray_Logo"
"jpeg"			"#Valve_Take_Screen_Shot"
"cheer"			"#Valve_Cheer"
"bug"			"#GameUI_ReportBug"
"quit"			"#Valve_Quit_Game"
"askconnect_accept"	"#Valve_Accept_Redirect"
"blank"		"=========================="
"blank"		"#Valve_Miscellaneous_Keyboard_Keys_Title"
"blank"		"=========================="
"+left"			"#Valve_Turn_Left"
"+right"		"#Valve_Turn_Right"
"+moveup"		"#Valve_Swim_Up"
"+movedown"		"#Valve_Swim_Down"
"+lookup"		"#Valve_Look_Up"
"+lookdown"		"#Valve_Look_Down"
"centerview"	"#Cstrike_Reset_View"
"+strafe"		"#Valve_Strafe_Modifier"
"+mlook"		"#Cstrike_Mouse_Look"
"+klook"		"#Cstrike_Keyboard_Look"
)KB";
const char kOurActions[] = R"KB("blank"		"=========================="
"blank"		"bhoptools"
"blank"		"=========================="
"bhoptools_predict_toggle"	"Toggle prediction (triggers, rngfix, checkpoints, HUD speed)"
)KB"
#ifdef _WIN32
R"KB("bhoptools_ape_toggle"	"Toggle BunnyhopAPE autobhop prediction"
)KB"
#endif
R"KB("cstv"		"Open / close CS:TV"
"cstv_book"	"Open / close the Progress Log"
"triggerlook_menu"	"Open the trigger menu"
"p2paint_menu"	"Open the paint menu"
"+paint"	"Paint (hold, then click)"
"+erase"	"Erase paint (hold, then click)"
)KB";
const char kDefaults[] = R"KB("h"				"commandmenu"
"k"				"+voicerecord"
"b"				"buymenu"
","				"buy primammo"
"."				"buy secammo"
"o"				"buyequip"
"m"				"teammenu"
"z"				"radio1"
"x"				"radio2"
"c"				"radio3"
"g"				"drop"
"n"				"nightvision"
"w"				"+forward"
"UPARROW"		"+forward"
"s"				"+back"
"DOWNARROW"		"+back"
"LEFTARROW"		"+left"
"RIGHTARROW"	"+right"
"a"				"+moveleft"
"d"				"+moveright"
"SPACE"			"+jump"
"CTRL"			"+duck"
"j"			"cheer"
"TAB"			"+showscores"
"i"				"showbriefing"
"e"				"+use"
"'"				"+moveup"
"/"				"+movedown"
"PGUP"			"+lookup"
"PGDN"			"+lookdown"
"END"			"centerview"
"ALT"			"+strafe"
"INS"			"+klook"
";"				"+mlook"
"r"				"+reload"
"SHIFT"			"+speed"
"MOUSE1"		"+attack"
"ENTER"			"+attack"
"MOUSE2"		"+attack2"
"\\"			"+attack2"
"f"				"impulse 100"
"t"				"impulse 201"
"1"				"slot1"
"2"				"slot2"
"3"				"slot3"
"4"				"slot4"
"5"				"slot5"
"6"				"slot6"
"7"				"slot7"
"8"				"slot8"
"9"				"slot9"
"0"				"slot10"
"MWHEELUP"		"invprev"
"["				"invprev"
"MWHEELDOWN"	"invnext"
"]"				"invnext"
"q"				"lastinv"
"F5"			"jpeg"
"F10"			"quit prompt"
"PAUSE"			"pause"
"ESCAPE"		"escape"
"~"				"toggleconsole"
"`"				"toggleconsole"
"+"				"sizeup"
"="				"sizeup"
"-"				"sizedown"
"y"				"messagemode"
"u"				"messagemode2"
"F1"			"autobuy"
"F2"			"rebuy"
"F4"			"bug"
"F3"			"askconnect_accept"
"F8"			"bhoptools_predict_toggle"
)KB";

inline void WriteIfChanged(const std::string& path, const std::string& text)
{
	if (FILE* f = fopen(path.c_str(), "rb")) {
		std::string old(text.size() + 1, '\0');
		old.resize(fread(&old[0], 1, old.size(), f));
		fclose(f);
		if (old == text) return;
	}
	if (FILE* f = fopen(path.c_str(), "wb")) {
		fwrite(text.data(), 1, text.size(), f);
		fclose(f);
	}
}

// cstrike: the game's cstrike folder, with a trailing separator
inline void Write(const std::string& cstrike)
{
	std::string dir = cstrike;
	for (const char* sub : { "custom", "/bhoptools", "/scripts" }) {
		dir += sub;
#ifdef _WIN32
		_mkdir(dir.c_str());
#else
		mkdir(dir.c_str(), 0755);
#endif
	}
	WriteIfChanged(dir + "/kb_act.lst", std::string(kOurActions) + kStockActions);   // ours at the top
	WriteIfChanged(dir + "/kb_def.lst", kDefaults);
}

}
