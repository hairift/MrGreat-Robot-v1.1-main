#pragma once

#include "mcp_server.h"
#include "servo_controller.h"

class ServoMcpTools {
public:
    ServoMcpTools() {
        auto& mcp  = McpServer::GetInstance();
        auto& ctrl = ServoController::GetInstance();

        // Satu alat untuk daya servo, baik menyalakan maupun mematikan
        mcp.AddTool("self.servo.power", "Servo ON/OFF (enable or disable). Gunakan ini sebelum self.servo.action jika servo masih mati.",
            PropertyList({Property("enable", kPropertyTypeBoolean)}),
            [&ctrl](const PropertyList& props) -> ReturnValue {
                bool en = props["enable"].value<bool>();
                if (en) ctrl.Enable(); else ctrl.Disable();
                return en ? std::string("servo ON") : std::string("servo OFF");
            });

        // Satu alat untuk aksi servo dengan parameter string
        // AI mengirim nama aksi seperti shake_head, turn_right, wave_right, dance, dan lain-lain.
        mcp.AddTool("self.servo.action", "Gerakkan servo robot. Kepala hanya bergerak kiri-kanan atau geleng, tidak ada gerakan mengangguk. "
            "Jika user meminta bagian tubuh tertentu, pilih action yang paling spesifik dan jangan gerakkan bagian lain kecuali memang diminta. "
            "Contoh: 'angkat tangan kanan' -> raise_right, 'lambaikan tangan kanan' -> wave_right, 'lihat kiri' -> turn_head_left. "
            "Gunakan action gabungan seperti wave_both, dance, atau menyapa hanya jika user memang meminta gerakan gabungan. "
            "Action: shake_head, turn_head_right, turn_head_left, head_center, wave_right, raise_right, straight_right, lower_right, wave_left, raise_left, straight_left, lower_left, wave_both, raise_both, straight_both, forward_both, dance, salam, hormat, tepuk_tangan, menyapa, reset",
            PropertyList({Property("action", kPropertyTypeString)}),
            [&ctrl](const PropertyList& props) -> ReturnValue {
                std::string act = props["action"].value<std::string>();
                ServoMove mv = ServoMove::NONE;
                if (act == "shake_head")       mv = ServoMove::HEAD_SHAKE;
                else if (act == "turn_head_right")  mv = ServoMove::HEAD_TURN_RIGHT;
                else if (act == "turn_head_left")   mv = ServoMove::HEAD_TURN_LEFT;
                else if (act == "head_center")      mv = ServoMove::HEAD_CENTER;
                else if (act == "wave_right")       mv = ServoMove::WAVE_RIGHT_ARM;
                else if (act == "raise_right")      mv = ServoMove::RAISE_RIGHT_ARM;
                else if (act == "straight_right")   mv = ServoMove::STRAIGHT_RIGHT_ARM;
                else if (act == "lower_right")      mv = ServoMove::LOWER_RIGHT_ARM;
                else if (act == "wave_left")        mv = ServoMove::WAVE_LEFT_ARM;
                else if (act == "raise_left")       mv = ServoMove::RAISE_LEFT_ARM;
                else if (act == "straight_left")    mv = ServoMove::STRAIGHT_LEFT_ARM;
                else if (act == "lower_left")       mv = ServoMove::LOWER_LEFT_ARM;
                else if (act == "wave_both")        mv = ServoMove::WAVE_BOTH_ARMS;
                else if (act == "raise_both")       mv = ServoMove::RAISE_BOTH_ARMS;
                else if (act == "straight_both")    mv = ServoMove::STRAIGHT_BOTH_ARMS;
                else if (act == "forward_both")     mv = ServoMove::STRAIGHT_BOTH_ARMS;
                else if (act == "dance")            mv = ServoMove::DANCE;
                else if (act == "salam")            mv = ServoMove::SALAM;
                else if (act == "hormat")           mv = ServoMove::HORMAT;
                else if (act == "tepuk_tangan")     mv = ServoMove::TEPUK_TANGAN;
                else if (act == "menyapa")          mv = ServoMove::MENYAPA;
                else if (act == "reset")            mv = ServoMove::RESET_POSITION;
                else return std::string("unknown action: " + act);
                if (!ctrl.IsEnabled()) {
                    return std::string("servo OFF: aktifkan dulu dengan self.servo.power");
                }
                ctrl.ExecuteMove(mv);
                return std::string("OK: " + act);
            });
    }
};
