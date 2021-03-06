/*
 * This file is part of Adblock Plus <https://adblockplus.org/>,
 * Copyright (C) 2006-2017 eyeo GmbH
 *
 * Adblock Plus is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * Adblock Plus is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Adblock Plus.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <map>
#include <AdblockPlus/JsValue.h>
#include <AdblockPlus/WebRequest.h>

#include "JsContext.h"
#include "Thread.h"
#include "Utils.h"
#include "WebRequestJsObject.h"

namespace
{
  class WebRequestThread : public AdblockPlus::Thread
  {
  public:
    WebRequestThread(const AdblockPlus::JsEnginePtr& jsEngine, const AdblockPlus::JsValueList& arguments)
      : Thread(true), jsEngine(jsEngine), url(arguments[0].AsString()),
        callback(arguments[2])
    {
      if (!url.length())
        throw std::runtime_error("Invalid string passed as first argument to GET");

      {
        const AdblockPlus::JsValue& headersObj = arguments[1];
        if (!headersObj.IsObject())
          throw std::runtime_error("Second argument to GET must be an object");

        std::vector<std::string> properties = headersObj.GetOwnPropertyNames();
        for (const auto& header : properties)
        {
          std::string headerValue = headersObj.GetProperty(header).AsString();
          if (header.length() && headerValue.length())
            headers.push_back(std::pair<std::string, std::string>(header, headerValue));
        }
      }

      if (!callback.IsFunction())
        throw std::runtime_error("Third argument to GET must be a function");
    }

    AdblockPlus::ServerResponse NotAllowedResponse()
    {
      AdblockPlus::ServerResponse result;
      result.status = AdblockPlus::WebRequest::NS_ERROR_CONNECTION_REFUSED;
      result.responseStatus = 0;
      return result;
    }

    void Run()
    {
      AdblockPlus::ServerResponse result = jsEngine->IsConnectionAllowed() ?
        jsEngine->GetWebRequest()->GET(url, headers) : NotAllowedResponse();

      AdblockPlus::JsContext context(*jsEngine);

      auto resultObject = jsEngine->NewObject();
      resultObject.SetProperty("status", result.status);
      resultObject.SetProperty("responseStatus", result.responseStatus);
      resultObject.SetProperty("responseText", result.responseText);

      auto headersObject = jsEngine->NewObject();
      for (const auto& header : result.responseHeaders)
      {
        headersObject.SetProperty(header.first, header.second);
      }
      resultObject.SetProperty("responseHeaders", headersObject);

      AdblockPlus::JsValueList params;
      params.push_back(resultObject);
      callback.Call(params);
    }

  private:
    AdblockPlus::JsEnginePtr jsEngine;
    std::string url;
    AdblockPlus::HeaderList headers;
    AdblockPlus::JsValue callback;
  };

  v8::Handle<v8::Value> GETCallback(const v8::Arguments& arguments)
  {
    WebRequestThread* thread;
    try
    {
      AdblockPlus::JsEnginePtr jsEngine = AdblockPlus::JsEngine::FromArguments(arguments);
      AdblockPlus::JsValueList converted = jsEngine->ConvertArguments(arguments);
      if (converted.size() != 3u)
        throw std::runtime_error("GET requires exactly 3 arguments");
      thread = new WebRequestThread(jsEngine, converted);
    }
    catch (const std::exception& e)
    {
      using AdblockPlus::Utils::ToV8String;
      v8::Isolate* isolate = arguments.GetIsolate();
      return v8::ThrowException(ToV8String(isolate, e.what()));
    }
    thread->Start();
    return v8::Undefined();
  }
}

AdblockPlus::JsValue& AdblockPlus::WebRequestJsObject::Setup(
    AdblockPlus::JsEngine& jsEngine, AdblockPlus::JsValue& obj)
{
  obj.SetProperty("GET", jsEngine.NewCallback(::GETCallback));
  return obj;
}
