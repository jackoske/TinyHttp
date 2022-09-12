#pragma once

#include <stdlib.h> 
#include "Basic/List.h"
#include "Server.h"
#include "Client.h"
#include "HttpClient.h"
#include "Server/HttpHeader.h"
#include "Server/HttpRequestHandlerLine.h"
#include "Server/HttpRequestRewrite.h"
#include "Extensions/Extension.h"
#include "Server/HttpChunkWriter.h"
#include <WiFi.h>


namespace tinyhttp {

/**
 * @brief A Simple Header only implementation of Http Server that allows the registration of callback
 * functions. This is based on the Arduino Server class.
 * 
 */
class HttpServer {
    public:
        HttpServer(WiFiServer &server_ptr, int bufferSize=1024){
            HttpLogger.log(Info,"HttpServer");
            this->server_ptr = &server_ptr;
            this->buffer_size = bufferSize;
            this->buffer = new char[bufferSize];
        }

        ~HttpServer(){
            HttpLogger.log(Info,"~HttpServer");
            delete []buffer;
            handler_collection.clear();
            request_header.clear(false);
            reply_header.clear(false);
            rewrite_collection.clear();
        }

        /// Starts the server on the indicated port - calls WiFi.begin(ssid, password);
        bool begin(int port, const char* ssid, const char* password){
            if (WiFi.status() != WL_CONNECTED && ssid!=nullptr && password!=nullptr){
                WiFi.begin(ssid, password);
                while (WiFi.status() != WL_CONNECTED) {        
                    delay(500);
                    Serial.print(".");
                }

                Serial.println();
                Serial.print("Started Server at ");
                Serial.print(WiFi.localIP());
                Serial.print(":");
                Serial.println(port);

            }
            return begin(port);
        }

        IPAddress &localIP() {
            static IPAddress address;
            address = WiFi.localIP();
            return address;
        }

        /// Starts the server on the indicated port
        bool begin(int port){
            HttpLogger.log(Info,"HttpServer","begin");
            is_active = true;
            server_ptr->begin(port);
            return true;
        }

        /// stops the server_ptr
        void stop(){
            HttpLogger.log(Info,"HttpServer","stop");
            is_active = false;
        }

        /// adds a rewrite rule
        void rewrite(const char* from, const char* to){
            HttpRequestRewrite *line = new HttpRequestRewrite(from, to);
            rewrite_collection.push_back(line);
        }


        /// register a generic handler
        void on(const char* url, MethodID method, web_callback_fn fn,void* ctx[]=nullptr, int ctxCount=0){
            HttpLogger.log(Info,"on-generic",url);
            HttpRequestHandlerLine *hl = new HttpRequestHandlerLine();
            hl->path = url;
            hl->fn = fn;
            hl->method = method;
            hl->context = ctx;
            hl->contextCount = ctxCount;
            addHandler(hl);
        }

        /// register a handler with mime
        void on(const char* url, MethodID method, const char* mime, web_callback_fn fn){
            HttpLogger.log(Info,"on-mime",url);
            HttpRequestHandlerLine *hl = new HttpRequestHandlerLine();
            hl->path = url;
            hl->fn = fn;
            hl->method = method;
            hl->mime = mime;
            addHandler(hl);
        }


        /// register a handler which provides the indicated string
        void on(const char* url, MethodID method, const char* mime, const char* result) {
            HttpLogger.log(Info,"on-strings");

            auto lambda = [](HttpServer *server_ptr,const char*requestPath, HttpRequestHandlerLine *hl) { 
                HttpLogger.log(Info,"on-strings","lambda");
                if (hl->contextCount<2){
                    HttpLogger.log(Error,"The context is not available");
                    return;
                }
                const char* mime = static_cast<Str*>(hl->context[0])->c_str();
                const char* msg = static_cast<Str*>(hl->context[1])->c_str();
                server_ptr->reply(mime, msg, 200);
            };
            HttpRequestHandlerLine *hl = new HttpRequestHandlerLine(2);
            hl->context[0] = new Str(mime);
            hl->context[1] = new Str(result);
            hl->path = url;
            hl->fn = lambda;
            hl->method = method;
            addHandler(hl);
        }
  
        /// register a redirection
        void on(const char*url, MethodID method, Url &redirect){
            HttpLogger.log(Info,"on-redirect");
            auto lambda = [](HttpServer *server_ptr, const char*requestPath, HttpRequestHandlerLine *hl) { 
                if (hl->contextCount<1){
                    HttpLogger.log(Error,"The context is not available");
                    return;
                }
                HttpLogger.log(Info,"on-redirect","lambda");
                HttpReplyHeader reply_header;
                Url *url = static_cast<Url*>(hl->context[0]);
                reply_header.setValues(301, "Moved");
                reply_header.put(LOCATION, url->url());
                reply_header.write(server_ptr->client());
                server_ptr->endClient(); 
            };

            HttpRequestHandlerLine *hl = new HttpRequestHandlerLine(1);
            hl->context[0] = new Url(redirect);
            hl->path = url;
            hl->fn = lambda;
            hl->method = method;
            addHandler(hl);
        }


        /// generic handler - you can overwrite this method to provide your specifc processing logic
        bool onRequest(const char* path) {
            HttpLogger.log(Info,"onRequest", path);

            bool result = false;
            // check in registered handlers
            Str pathStr = Str(path);
            for (auto it = handler_collection.begin() ; it != handler_collection.end(); ++it) {
                HttpRequestHandlerLine *handler_line_ptr = *it;
                HttpLogger.log(Info,"onRequest - checking:",handler_line_ptr->path);

                if (pathStr.matches(handler_line_ptr->path) 
                && request_header.method() == handler_line_ptr->method
                && matchesMime(handler_line_ptr->mime, request_header.accept())) {
                    // call registed handler function
                    HttpLogger.log(Info,"onRequest","->found");
                    handler_line_ptr->fn(this, path, handler_line_ptr);
                    result = true;
                    break;
                }
            }
            return result;
        }

        // chunked  reply with a full input stream
        void reply(const char* contentType, Stream &inputStream, int status=200, const char* msg=SUCCESS) {
            HttpLogger.log(Info,"reply","stream");
            reply_header.setValues(status, msg);
            reply_header.put(TRANSFER_ENCODING,CHUNKED);
            reply_header.put(CONTENT_TYPE,contentType);
            reply_header.put(CONNECTION,CON_KEEP_ALIVE);
            reply_header.write(this->client());
            HttpChunkWriter chunk_writer;
            while (inputStream.available()){
                int len = inputStream.readBytes(buffer, buffer_size);
                chunk_writer.writeChunk(*client_ptr,(const char*)buffer, len);
            }
            // final chunk
            chunk_writer.writeEnd(*client_ptr);
            endClient();
        }

        void replyChunked(const char* contentType, int status=200, const char* msg=SUCCESS) {
            HttpLogger.log(Info,"reply","replyChunked");
            reply_header.setValues(status, msg);
            reply_header.put(TRANSFER_ENCODING,CHUNKED);
            reply_header.put(CONTENT_TYPE,contentType);
            reply_header.put(CONNECTION,CON_KEEP_ALIVE);
            reply_header.write(this->client());
        }

        // write reply - stream with header size
        void reply(const char* contentType, Stream &inputStream, int size, int status=200, const char* msg=SUCCESS){
            HttpLogger.log(Info,"reply","stream");
            reply_header.setValues(status, msg);
            reply_header.put(CONTENT_LENGTH,size);
            reply_header.put(CONTENT_TYPE,contentType);
            reply_header.put(CONNECTION,CON_KEEP_ALIVE);
            reply_header.write(this->client());

            while (inputStream.available()){
                int len = inputStream.readBytes(buffer, buffer_size);
                int written = client_ptr->write((const uint8_t*)buffer, len);
            }
            //inputStream.close();
            endClient();
        }

        // write reply - string with header size
        void reply(const char* contentType, const char* str, int status=200, const char* msg=SUCCESS){
            HttpLogger.log(Info,"reply","str");
            int len = strlen(str);
            reply_header.setValues(status, msg);
            reply_header.put(CONTENT_LENGTH,len);
            reply_header.put(CONTENT_TYPE,contentType);
            reply_header.put(CONNECTION,CON_KEEP_ALIVE);
            reply_header.write(this->client());
            client_ptr->write((const uint8_t*)str, len);
            endClient();
        }

        void replyNotFound() {
            HttpLogger.log(Info,"reply","404");
            reply(404,"Page Not Found" );
        }

        void reply(int status, const char* msg) {
            HttpLogger.log(Info,"reply","status");
            reply_header.setValues(404, "Page Not Found");
            reply_header.write(this->client());
            endClient();
        }

        /// provides the request header
        HttpRequestHeader &requestHeader() {
            return request_header;
        }     

        /// provides the reply header
        HttpReplyHeader & replyHeader() {
            return reply_header;
        }   

        /// closes the connection to the current client_ptr
        void endClient() {
            HttpLogger.log(Info,"HttpServer","endClient");
            client_ptr->flush();
            client_ptr->stop();
        }

        // print a CR LF
        void crlf() {
            client_ptr->print("\r\n");
            client_ptr->flush();
        }

        // registers an extension
        void addExtension(Extension &out){
            HttpLogger.log(Info,"HttpServer","addExtension");
            out.open(this);
            extension_collection.push_back(&out);
        }

        // adds a new handler
        void addHandler(HttpRequestHandlerLine *handlerLinePtr){
            handler_collection.push_back(handlerLinePtr);
        }

        /// Legacy method: same as copy();
        void doLoop(){
            copy();
        }

        /// Call this method from your loop!
        void copy(){
            // get the actual client_ptr
            if (is_active) {
                WiFiClient client;
                if (server_ptr->hasClient()){
                    HttpLogger.log(Info,"doLoop->hasClient");
                    client = server_ptr->available();
                    client_ptr = &client;
                }

                // process the new client with standard functionality
                if (client && client.available()>10) {
                    processRequest();
                }

                // process doLoop of all registed (and opened) extension_collection 
                processextension_collection();
            }
        }

        // return the current client
        WiFiClient &client() {
            return *client_ptr;
        }

        operator bool(){
            return is_active;
        }

    protected:
        // data
        HttpRequestHeader request_header;
        HttpReplyHeader reply_header;
        List<HttpRequestHandlerLine*> handler_collection;
        List<Extension*> extension_collection;
        List<HttpRequestRewrite*> rewrite_collection;
        WiFiClient *client_ptr;
        WiFiServer *server_ptr;
        bool is_active;
        char* buffer;
        int buffer_size;


        // process a full request and send the reply
        void processRequest(){
            HttpLogger.log(Info,"processRequest");
            request_header.read(this->client());
            // provde reply with empty header
            reply_header.clear();
            // determine the path
            const char* path = request_header.urlPath();
            path = resolveRewrite(path);
            bool processed = onRequest(path);
            if (!processed){
              //  replyNotFound();
            }                   
        }

        /// executes the doLoop of all extension_collection
        void processextension_collection(){
            //if (extension_collection.size()>0) HttpLogger.log(Info,"processextension_collection");
            // we handle all open clients
            for (auto i = extension_collection.begin(); i != extension_collection.end(); ++i) {
                Extension *ext = (*i);
                // register new client
                ext->doLoop();
            }    
        }

        /// determiens the potentially rewritten url which should be used for the further processing
        const char *resolveRewrite(const char* from){
             for (auto i = rewrite_collection.begin(); i != rewrite_collection.end(); ++i) {
                HttpRequestRewrite *rewrite = (*i);
                if (rewrite->from.matches(from)){
                    return rewrite->to.c_str();
                }
            }    
            return from;
        }

        bool matchesMime(const char *handler_mime, const char* request_mime) {
            if (handler_mime==nullptr || request_mime==nullptr){
                return true;
            }
            bool result = Str(request_mime).contains(handler_mime);
            return result;
        }


};

}

