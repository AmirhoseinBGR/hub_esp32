#include "remote_settings.h"
#include "web_api.h"
#include "uart_link.h"
#include <ArduinoJson.h>

static uint32_t next_token, token, started;
static bool pending;
static char result[160];
static void expire()
{
    if(pending && millis()-started>15000u) {
        pending=false;
        strlcpy(result,"تأیید نشد؛ هاب پاسخ نداد",sizeof(result));
    }
}
void remote_settings_result(const char* line)
{
    if(strncmp(line,"CFG:RES|",8)) return;
    const char* p=line+8; char* end=nullptr;
    unsigned long id=strtoul(p,&end,10);
    if(end==p || *end!='|' || !pending || id!=token) return;
    strlcpy(result,end+1,sizeof(result)); pending=false;
}
void remote_settings_routes()
{
    server.on("/api/settings",HTTP_POST,[] {
        expire();
        if(pending) {server.send(409,"application/json","{\"error\":\"busy\"}");return;}
        JsonDocument req;
        if(deserializeJson(req,server.arg("plain")) || !req["cmd"].is<const char*>()) {
            server.send(400,"application/json","{\"error\":\"command required\"}");return;
        }
        const char* cmd=req["cmd"];
        size_t len=strlen(cmd);
        if(!len || len>150) {server.send(400,"application/json","{\"error\":\"length\"}");return;}
        for(const unsigned char* p=(const unsigned char*)cmd;*p;p++) {
            if(*p<32 || *p>126 || *p=='|') {
                server.send(400,"application/json","{\"error\":\"invalid command\"}");return;
            }
        }
        if(!next_token) next_token=millis();
        token=++next_token; if(!token) token=++next_token;
        started=millis(); result[0]=0; pending=true;
        uart_send("CFG:REQ|%lu|%s",(unsigned long)token,cmd);
        // Accepted is NOT applied: client must poll the final correlated reply.
        server.send(202,"application/json",String("{\"accepted\":true,\"token\":")+token+"}");
    });
    server.on("/api/settings",HTTP_GET,[] {
        expire();
        if(!server.hasArg("token") || strtoul(server.arg("token").c_str(),nullptr,10)!=token || !token) {
            server.send(404,"application/json","{\"error\":\"unknown token\"}");return;
        }
        JsonDocument doc; doc["token"]=token; doc["pending"]=pending;
        if(!pending) doc["text"]=result;
        String out; serializeJson(doc,out);
        server.send(200,"application/json; charset=utf-8",out);
    });
}
