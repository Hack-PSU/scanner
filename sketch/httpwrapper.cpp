#include "httpwrapper.h"


namespace hackPSU {
	bool HTTPImpl::getAPIKey(){


		String url = "https://"+redisHost+"/auth/register-scanner";
		String payload = "{\"pin\":"+MASTER_KEY+"}";
		int headerCount = 1;
		Headers headers [] = { { "Content-Type", "application/json" } };


		Response* response = HTTP::POST(url, payload, headerCount, headers);

		if (response->responseCode < 0){
			Serial.print("Http request failed: ");
			Serial.println(HTTP::handleError(response->responseCode));
			//Free up memory since parsing is complete
			delete response;
			return false;
		}

		StaticJsonBuffer<200> jsonBuffer;
		JsonObject& root = jsonBuffer.parseObject(response->payload);

		//Free up memory since parsing is complete
		delete response;

		//Redis json parse
		String status = root["status"];
		String data = root["data"];
		String message = root["message"];	//Should message also be returned to display why user was not allowed in?
		apiKey = data["apikey"];
  		//The following is based on assumptions and should be checked
  		return (status == "success");
	}

	redisData* HTTPImpl::getDataFromPin(String pin){
		String url = "http://"+redisHost+"/tabs/getpin";
    	Serial.println(url);
		String payload = "{\"pin\":"+pin+"}";
		int headerCount = 1;
		Headers headers [] = { { "Content-Type", "application/json" } };


		Response* response = HTTP::POST(url, payload, headerCount, headers);

		if (response->responseCode < 0){
			Serial.print("Http request failed: ");
			Serial.println(HTTP::handleError(response->responseCode));
			delete response;
			return false;
		}

    	Serial.println(response->payload);

		StaticJsonBuffer<200> jsonBuffer;
		JsonObject& root = jsonBuffer.parseObject(response->payload);

		//Free up memory since parsing is complete
		delete response;

//		if (!root.success()) {
//			throw "json parsing failed :( lit";
//  		}

  		//Redis json parse
		redisData* pinData = new redisData;
		pinData->uid = root["uid"];
		pinData->pin = root["pin"];
		pinData->name = root["name"];
		pinData->shirtSize = root["shirtSize"];
		pinData->diet = root["diet"];
		pinData->counter = root["counter"];
		pinData->numScans = root["numScans"];

  		return pinData;
	}

	bool HTTPImpl::assignRfidToUser(String rfidCode, String pin){

		String url = "https://"+redisHost+"/tabs/setup";
		String payload = "{\"id\":"+rfidCode+",\"pin\":"+pin+"}";
		int headerCount = 1;
		Headers headers [] = { { "Content-Type", "application/json" } };


		Response* response = HTTP::POST(url, payload, headerCount, headers);

		if (response->responseCode < 0){
			Serial.print("Http request failed: ");
			Serial.println(HTTP::handleError(response->responseCode));
			//Free up memory since parsing is complete
			delete response;
			return false;
		}

		StaticJsonBuffer<200> jsonBuffer;
		JsonObject& root = jsonBuffer.parseObject(response->payload);

		//Free up memory since parsing is complete
		delete response;

//		if (!root.success()) {
//			throw "json parsing failed :(";
//  		}

  		//Redis json parse
  		return root["status"] == "success";
	}

	bool HTTPImpl::entryScan(String locationId, String rfidTag){


		String url = "https://"+redisHost+"/tabs/add";
		String payload = "{\"location\":"+locationId+",\"id\":"+rfidTag+"}";
		int headerCount = 1;
		Headers headers [] = { { "Content-Type", "application/json" } };


		Response* response = HTTP::POST(url, payload, headerCount, headers);

		if (response->responseCode < 0){
			Serial.print("Http request failed: ");
			Serial.println(HTTP::handleError(response->responseCode));
			//Free up memory since parsing is complete
			delete response;
			return false;
		}



		StaticJsonBuffer<200> jsonBuffer;
		JsonObject& root = jsonBuffer.parseObject(response->payload);

//		if (!root.success()) {
//			throw "json parsing failed :(";
//  		}

		//Free up memory since parsing is complete
		delete response;

		//Redis json parse
		String status = root["status"];
		String data = root["data"];
		String message = root["message"];	//Should message also be returned to display why user was not allowed in?

  		//The following is based on assumptions and should be checked
  		return (status == "success");
	}

  Location* HTTPImpl::getLocations(void){
    String url = "https://"+redisHost+"/tabs/active-locations";
    Response* response = HTTP::GET(url);

    if (response->responseCode < 0) {
      display->print("GET REQUEST FAIL", 1);
    }

    StaticJsonBuffer<200> jsonBuffer;
    JsonObject& root = jsonBuffer.parseObject(response->payload);

    delete response;
    int len = root["length"];
    Location* locations = new Location[len];

    for(int i = 0; i < len; i++0{
      locations[i] = {.};
    }

  }


}
