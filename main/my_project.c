
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "esp_netif.h"
#include "esp_http_server.h"
    #include <stdio.h>

/* The examples use WiFi configuration that you can set via project configuration menu

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/
#define EXAMPLE_ESP_WIFI_SSID      "viettel"
#define EXAMPLE_ESP_WIFI_PASS      "123456789"
#define EXAMPLE_ESP_MAXIMUM_RETRY   5

#define AP_SSID                    "ESP32_AP"
#define AP_PASS                    "12345678"
#define AP_MAX_CONNECTIONS         4
#define AP_CHANNEL                 1
#define MAX_SSID_LENGTH 32     // Định nghĩa kích thước tối đa cho SSID.
#define MAX_PASS_LENGTH 64     // Định nghĩa kích thước tối đa cho mật khẩu Wi-Fi.

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static const char *TAG = "wifi station";

static int s_retry_num = 0;


// Hàm xử lý yêu cầu HTTP GET để hiển thị giao diện cấu hình
esp_err_t index_get_handler(httpd_req_t *req) {   
    const char* response = "<!DOCTYPE html>" //HTML chứa giao diện cấu hình SSID và mật khẩu. Đây là form HTML mà người dùng sẽ thấy khi kết nối vào Wi-Fi của ESP32
                           "<html><body>"
                           "<h2>ESP32 Wi-Fi Config</h2>"
                           "<form action=\"/save\" method=\"post\">"
                           "<label for=\"ssid\">SSID:</label><br>"
                           "<input type=\"text\" id=\"ssid\" name=\"ssid\"><br>"
                           "<label for=\"password\">Password:</label><br>"
                           "<input type=\"text\" id=\"password\" name=\"password\"><br><br>"
                           "<input type=\"submit\" value=\"Save\">"
                           "</form></body></html>";
    httpd_resp_send(req, response, strlen(response));  //Gửi phản hồi HTML đến trình duyệt.
    return ESP_OK;
}   

// Hàm xử lý yêu cầu HTTP POST để lưu cấu hình
esp_err_t save_post_handler(httpd_req_t *req) {
    char buf[100];  //Bộ đệm để lưu dữ liệu nhận từ yêu cầu HTTP
    int ret, total_len = 0;
    while ((ret = httpd_req_recv(req, buf + total_len, sizeof(buf) - total_len)) > 0) {
        total_len += ret;
        if (total_len >= sizeof(buf) - 1) {
            break;
        }
    }
    if (ret < 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    buf[total_len] = '\0';  //Kết thúc chuỗi 

    // Tìm SSID và Password trong dữ liệu POST
    char *ssid_start = strstr(buf, "ssid="); //Tìm vị trí bắt đầu của SSID trong dữ liệu nhận được.
    char *pass_start = strstr(buf, "password="); //Tìm vị trí bắt đầu của mật khẩu trong dữ liệu nhận được.
    if (ssid_start && pass_start) {
        ssid_start += 5;
        pass_start += 9;

        char *ssid_end = strchr(ssid_start, '&');
        char *pass_end = strchr(pass_start, '\0');
        if (ssid_end) {
            *ssid_end = '\0';
        }
        if (pass_end) {
            *pass_end = '\0';
        }

        ESP_LOGI(TAG, "Received SSID: %s", ssid_start);
        ESP_LOGI(TAG, "Received Password: %s", pass_start);

        // Lưu cấu hình vào NVS
        nvs_handle my_handle;
        esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_handle);
        if (err == ESP_OK) {
              nvs_set_str(my_handle, "ssid", ssid_start);   // Lưu SSID vào NVS.
           nvs_set_str(my_handle, "password", pass_start); //Lưu mật khẩu vào NVS
            nvs_commit(my_handle);
         nvs_close(my_handle);
        }

        // Đăng lại trang xác nhận
        const char* response = "<html><body><h2>Settings Saved</h2><p>Restarting...</p></body></html>";  //HTML thông báo rằng cài đặt đã được lưu.
        httpd_resp_send(req, response, strlen(response)); //Gửi phản hồi HTML cho trình duyệt.
        vTaskDelay(2000 / portTICK_PERIOD_MS); // Đợi 2 giây trước khi khởi động lại.
        esp_restart(); // Khởi động lại ESP32.
    } else {
        httpd_resp_send_404(req);
    }
    return ESP_OK;
}

/* Global handle for HTTP server */
static httpd_handle_t server = NULL;


// Cấu hình và khởi động web server
httpd_handle_t start_webserver() {  //Hàm khởi động web server.
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();  // Cấu hình mặc định cho web server.
    
    ESP_LOGI(TAG, "Starting web server...");

    config.server_port = 80;
    if (httpd_start(&server, &config) == ESP_OK) {  //Khởi động web server và kiểm tra nếu thành công.
        httpd_uri_t index_uri = {
            .uri       = "/post",
            .method    = HTTP_GET,
            .handler   = index_get_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &index_uri);//Đăng ký URI để xử lý GET request.

        httpd_uri_t save_uri = {  //Định nghĩa URI cho việc lưu cài đặt (POST request).
            .uri       = "/save", 
            .method    = HTTP_POST,
            .handler   = save_post_handler,
            .user_ctx  = NULL
        };
       ESP_ERROR_CHECK( httpd_register_uri_handler(server, &save_uri));  //Đăng ký URI để xử lý POST request.
        ESP_LOGI(TAG, "Web server started successfully");

    } else{ESP_LOGE(TAG, "Start web server Fail");
             }
    
    return server;  // Trả về handle của web server.
}
// Dừng web server. 
void stop_webserver(httpd_handle_t server) {  
    if (server != NULL) {
        httpd_stop(server);  // Dừng web server nếu server đang chạy.
        ESP_LOGI(TAG, "Web server stopped.");
    }
}


// Khởi tạo chế độ Access Point
void wifi_init_ap(void) {
    esp_netif_create_default_wifi_ap();

    wifi_config_t wifi_ap_config = {
        .ap = {
            .ssid = AP_SSID,
            .ssid_len = strlen(AP_SSID),
            .password = AP_PASS,
            .max_connection = AP_MAX_CONNECTIONS,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,
            .channel = AP_CHANNEL,
        },
    };

    if (strlen(AP_PASS) == 0) {
        wifi_ap_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Lấy địa chỉ IP trong chế độ AP
    esp_netif_ip_info_t ip_info;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    esp_netif_get_ip_info(netif, &ip_info);
    ESP_LOGI(TAG, "ESP32 is now an Access Point with IP: " IPSTR, IP2STR(&ip_info.ip));
}    


static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) { 
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the STA");
        } else {
            ESP_LOGI(TAG, "Failed to connect to Wi-Fi, switching to Access Point mode.");
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            esp_wifi_stop(); // Dừng Wi-Fi STA trước khi chuyển sang AP
            wifi_init_ap();  // Chuyển sang chế độ Access Point
            start_webserver();
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init_sta(const char *ssid, const char *password)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
           .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));  //Sao chép SSID vào cấu hình.
    strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password));  //Sao chép mật khẩu vào cấu hình.
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 ssid, password);
                   stop_webserver(server);
    } else if (bits & WIFI_FAIL_BIT) { 
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
               ssid, password);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
}

void app_main(void)
{
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(ret);
    
     // Đọc thông tin cấu hình từ NVS
    nvs_handle my_handle;  //Biến để lưu handle của NVS.
    size_t required_size;  //Biến để lưu kích thước cần thiết để đọc SSID và mật khẩu.
    char ssid[MAX_SSID_LENGTH];  //Biến để lưu SSID.
    char password[MAX_PASS_LENGTH];  //Biến để lưu mật khẩu.

    ret = nvs_open("storage", NVS_READWRITE, &my_handle);  //Mở NVS để đọc và ghi.

    if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Error (%s) opening NVS!", esp_err_to_name(ret));

}   

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "opening NVS! done ");
        ret = nvs_get_str(my_handle, "ssid", NULL, &required_size);
        if (ret == ESP_OK) {
            nvs_get_str(my_handle, "ssid", ssid, &required_size);   //Đọc kích thước của SSID từ NVS.
        }
        required_size = MAX_PASS_LENGTH; // Reset or assign max buffer size for password.
        ret = nvs_get_str(my_handle, "password", NULL, &required_size);
    if (ret == ESP_OK) {
            nvs_get_str(my_handle, "password", password, &required_size);
     }
            nvs_close(my_handle);   //Đóng handle NVS.

    
    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    wifi_init_sta(ssid, password);
        
    } else 
    {
        wifi_init_ap();
        start_webserver();
}
}
