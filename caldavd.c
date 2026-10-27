/* ============================================================================
 * CalDAV Server & HTTP Web Server - C Implementation with Win32 GUI
 *
 * COMPILATION INSTRUCTIONS:
 *
 * With GCC (MinGW-w64):
 *   g cc -Os -s -Wl,--subsystem,windows -mwindows -o caldavd.exe caldavd.c -lgdi32 -lole32 -limm32 -lcomdlg32 -lcomctl32 -lws2_32 -ladvapi32
 *  gcc -g -O0 -Wl,--subsystem,windows -mwindows -o caldavd.exe caldavd.c -lgdi32 -lole32 -limm32 -lcomdlg32 -lcomctl32 -lws2_32 -ladvapi32 -lsecur32 -lcrypt32
 *  g cc -O3 -Wl,--subsystem,windows -mwindows -o caldavd.exe caldavd.c -lgdi32 -lole32 -limm32 -lcomdlg32 -lcomctl32 -lws2_32 -ladvapi32 -lsecur32 -lcrypt32
 * 
 * 2026 HUGO
 * 
 * THIS WORK IS NOT FIT FOR ANY FUNCTION OR PURPOSE, COMES WITH NO WARRANTY,
 * AND IS BEING RELEASED INTO THE PUBLIC DOMAIN.
 * ============================================================================ */
#define WIN32_LEAN_AND_MEAN
#define SECURITY_WIN32
#include <windows.h>
#include <shellapi.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <commctrl.h>
#include <wincrypt.h>
#include <security.h>
#include <schannel.h>
#include <commdlg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <process.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "imm32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "secur32.lib")
#pragma comment(lib, "crypt32.lib")
#define SERVER_NAME "CaldAV-D"
#define SERVER_VERSION "2.0-TLS"
#define DEFAULT_PORT 443
#define DEFAULT_HTTP_PORT 8443
#define DEFAULT_TIMEOUT_MS 2000
#define MAX_CONNECTIONS 20
#define BUFFER_SIZE 8192
#define CONFIG_FILE ".\\caldavd.ini"
#define DATA_DIR "./calendars"
#define WEB_DIR "./"
#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY_ICON 1
#define ID_MENU_START 2
#define ID_MENU_STOP 3
#define ID_MENU_SETTINGS 4
#define ID_MENU_EXIT 5
#define ID_BTN_START 6
#define ID_BTN_STOP 7
#define ID_EDIT_PORT 8
#define ID_EDIT_TIMEOUT 10
#define ID_CHK_ANON 11
#define ID_EDIT_HTTP_PORT 12
#define ID_CHK_TLS 13
#define ID_LIST_USERS 15
#define ID_EDIT_USER 16
#define ID_EDIT_PASS 17
#define ID_BTN_ADDUSER 18
#define PORT_UPDATE_DELAY_MS 1000
#define ID_BTN_INSTALL_CERT 19
#define ID_BTN_EXPORT_CERT 20
#define ID_BTN_CERTMGR 21
#define ID_CHK_EDITALL 22
#define ID_MENU_IMPORT 30
#define ID_MENU_EXPORT 31
#define CA_CONTAINER_W L"CalDAV_RootCA_Container"
#define USER_CONTAINER_W L"CalDAV_Leaf_Container"
static HINSTANCE g_hInstance = NULL;
static HWND g_hMainWnd = NULL;
static HWND g_hSettingsWnd = NULL;
static NOTIFYICONDATA g_nid = { sizeof(NOTIFYICONDATA) };
static BOOL g_bRunning = FALSE;
static SOCKET g_ListenSocket = INVALID_SOCKET;
static SOCKET g_HttpListenSocket = INVALID_SOCKET;
static int g_Port = DEFAULT_PORT;
static int g_HttpPort = DEFAULT_HTTP_PORT;
static int g_TimeoutMs = DEFAULT_TIMEOUT_MS;
static BOOL g_bAnonymous = TRUE;
static BOOL g_bUseTls = TRUE;
static UINT g_TaskbarCreatedMsg = 0;
static WNDPROC OriginalListBoxProc = NULL;

static LRESULT CALLBACK ListBoxSubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_KEYDOWN && wp == VK_DELETE) {
        int idx = (int)SendMessage(hwnd, LB_GETCURSEL, 0, 0);
        if (idx != LB_ERR) {
            char user[256];
            SendMessage(hwnd, LB_GETTEXT, idx, (LPARAM)user);
            WritePrivateProfileString("users", user, NULL, CONFIG_FILE);
            WritePrivateProfileString("perms", user, NULL, CONFIG_FILE);
            SendMessage(hwnd, LB_DELETESTRING, idx, 0);
        }
        return 0;
    }
    return CallWindowProc(OriginalListBoxProc, hwnd, msg, wp, lp);
}

static CredHandle g_SrvCreds;
static BOOL g_TlsInitialized = FALSE;

static PCCERT_CONTEXT g_pTlsCert = NULL;
typedef struct {
    SOCKET socket;
    int port;
    BOOL use_tls;
    CtxtHandle hCtx;
    SecPkgContext_StreamSizes sizes;
    char* io_buf;
    int io_len;
} ClientThreadCtx;

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK SettingsWndProc(HWND, UINT, WPARAM, LPARAM);
VOID CALLBACK UpdateTimerCallback(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime);
unsigned __stdcall ServerThread(void* pParam);
unsigned __stdcall ClientHandler(void* pParam);
unsigned __stdcall HttpServerThread(void* pParam);
unsigned __stdcall HttpClientHandler(void* pParam);

BOOL InitTLS(void);
VOID CleanupTLS(void);
BOOL TlsAccept(ClientThreadCtx* ctx);
int TlsRecv(ClientThreadCtx* ctx, char* buf, int len);
int TlsSend(ClientThreadCtx* ctx, const char* buf, int len);

INT_PTR CreateSettingsWindow(void);
VOID CreateTrayIcon(HWND hWnd);
VOID RemoveTrayIcon(void);
VOID ShowContextMenu(HWND hWnd);
VOID HandleCommandLine(ClientThreadCtx* ctx, char* buffer, char* response, const char* authUser);
VOID SendHTTPResponse(ClientThreadCtx* ctx, const char* status, const char* contentType, const char* body, SIZE_T bodyLen, const char* extraHeaders);
VOID SendHTTPFileResponse(ClientThreadCtx* ctx, const char* filePath);
BOOL SaveConfig(void);
BOOL LoadConfig(void);
BOOL StartServer(int port);
VOID StopServer(VOID);
BOOL StartHttpServer(int port);
VOID StopHttpServer(VOID);
VOID SeedDefaultUser(VOID);
void GetHTTPMethod(char* buffer, char* outMethod, int maxLen);
void GetRequestPath(char* buffer, char* outPath, int maxLen);

BOOL AuthenticateRequest(char* buffer, char* outUser);
VOID GenerateSaltedHash(const char* password, const char* salt, char* outHex);
VOID Base64Decode(const char* input, char* output, int maxLen);
VOID PopulateUserList(HWND listbox);

PCCERT_CONTEXT GenerateCertificate(void) {
    HCRYPTPROV hCAProv = 0, hUserProv = 0;
    HCRYPTKEY hCAKey = 0, hUserKey = 0;
    PCCERT_CONTEXT pCACert = NULL, pUserCert = NULL;
    PCERT_PUBLIC_KEY_INFO pUserPubKey = NULL;
    BYTE *pbUserCert = NULL, *pbEncodedSAN = NULL;
    CERT_NAME_BLOB caNameBlob = {0}, userNameBlob = {0};
    
    DWORD cbEncoded = 0;
    CertStrToNameA(X509_ASN_ENCODING, "CN=CalDAV Local Root CA", CERT_OID_NAME_STR, NULL, NULL, &cbEncoded, NULL);
    caNameBlob.pbData = (BYTE*)malloc(cbEncoded); caNameBlob.cbData = cbEncoded;
    CertStrToNameA(X509_ASN_ENCODING, "CN=CalDAV Local Root CA", CERT_OID_NAME_STR, NULL, caNameBlob.pbData, &caNameBlob.cbData, NULL);

    CRYPT_ALGORITHM_IDENTIFIER sigAlg = { szOID_RSA_SHA256RSA, {0, NULL} };

    if (!CryptAcquireContextW(&hCAProv, CA_CONTAINER_W, MS_ENH_RSA_AES_PROV_W, PROV_RSA_AES, 0)) {
        if (!CryptAcquireContextW(&hCAProv, CA_CONTAINER_W, MS_ENH_RSA_AES_PROV_W, PROV_RSA_AES, CRYPT_NEWKEYSET)) return NULL;
        CryptGenKey(hCAProv, AT_SIGNATURE, (2048 << 16) | CRYPT_EXPORTABLE, &hCAKey);

        CRYPT_KEY_PROV_INFO caKeyInfo = {0};
        caKeyInfo.pwszContainerName = (LPWSTR)CA_CONTAINER_W;
        caKeyInfo.pwszProvName = (LPWSTR)MS_ENH_RSA_AES_PROV_W;
        caKeyInfo.dwProvType = PROV_RSA_AES;
        caKeyInfo.dwKeySpec = AT_SIGNATURE;

        pCACert = CertCreateSelfSignCertificate(hCAProv, &caNameBlob, 0, &caKeyInfo, &sigAlg, NULL, NULL, NULL);

        if (pCACert) {
            HCERTSTORE hStore = CertOpenStore(CERT_STORE_PROV_SYSTEM, 0, 0, CERT_SYSTEM_STORE_CURRENT_USER, L"Root");
            if (hStore) {
                CertAddCertificateContextToStore(hStore, pCACert, CERT_STORE_ADD_REPLACE_EXISTING, NULL);
                CertCloseStore(hStore, 0);
            }
        }
    } else {
        HCERTSTORE hStore = CertOpenStore(CERT_STORE_PROV_SYSTEM, 0, 0, CERT_SYSTEM_STORE_CURRENT_USER, L"Root");
        if (hStore) {
            pCACert = CertFindCertificateInStore(hStore, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0, CERT_FIND_SUBJECT_STR_A, "CalDAV Local Root CA", NULL);
            CertCloseStore(hStore, 0);
        }
        if (!pCACert) {
            CryptReleaseContext(hCAProv, 0);
            CryptAcquireContextW(&hCAProv, CA_CONTAINER_W, MS_ENH_RSA_AES_PROV_W, PROV_RSA_AES, CRYPT_DELETEKEYSET);
            free(caNameBlob.pbData);
            return NULL;
        }
    }

    CertStrToNameA(X509_ASN_ENCODING, "CN=localhost", CERT_OID_NAME_STR, NULL, NULL, &cbEncoded, NULL);
    userNameBlob.pbData = (BYTE*)malloc(cbEncoded); userNameBlob.cbData = cbEncoded;
    CertStrToNameA(X509_ASN_ENCODING, "CN=localhost", CERT_OID_NAME_STR, NULL, userNameBlob.pbData, &userNameBlob.cbData, NULL);

    CryptAcquireContextW(&hUserProv, USER_CONTAINER_W, MS_ENH_RSA_AES_PROV_W, PROV_RSA_AES, CRYPT_DELETEKEYSET);
    CryptAcquireContextW(&hUserProv, USER_CONTAINER_W, MS_ENH_RSA_AES_PROV_W, PROV_RSA_AES, CRYPT_NEWKEYSET);
    CryptGenKey(hUserProv, AT_KEYEXCHANGE, (2048 << 16) | CRYPT_EXPORTABLE, &hUserKey);

    DWORD cbPubKey = 0;
    CryptExportPublicKeyInfo(hUserProv, AT_KEYEXCHANGE, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, NULL, &cbPubKey);
    pUserPubKey = (PCERT_PUBLIC_KEY_INFO)malloc(cbPubKey);
    CryptExportPublicKeyInfo(hUserProv, AT_KEYEXCHANGE, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, pUserPubKey, &cbPubKey);

    CERT_ALT_NAME_ENTRY sanEntries[3];
    sanEntries[0].dwAltNameChoice = CERT_ALT_NAME_DNS_NAME;
    sanEntries[0].pwszDNSName = L"localhost";
    BYTE ipv4_local[4] = {127, 0, 0, 1};
    sanEntries[1].dwAltNameChoice = CERT_ALT_NAME_IP_ADDRESS;
    sanEntries[1].IPAddress.cbData = sizeof(ipv4_local);
    sanEntries[1].IPAddress.pbData = ipv4_local;
    BYTE ipv4_lan[4] = {192, 168, 1, 57};
    sanEntries[2].dwAltNameChoice = CERT_ALT_NAME_IP_ADDRESS;
    sanEntries[2].IPAddress.cbData = sizeof(ipv4_lan);
    sanEntries[2].IPAddress.pbData = ipv4_lan;

    CERT_ALT_NAME_INFO sanInfo = { 3, sanEntries };
    DWORD cbSanEncoded = 0;
    CryptEncodeObjectEx(X509_ASN_ENCODING, X509_ALTERNATE_NAME, &sanInfo, CRYPT_ENCODE_ALLOC_FLAG, NULL, &pbEncodedSAN, &cbSanEncoded);

    CERT_EXTENSION extArray[2] = {0};
    extArray[0].pszObjId = szOID_SUBJECT_ALT_NAME2;
    extArray[0].fCritical = FALSE;
    extArray[0].Value.cbData = cbSanEncoded;
    extArray[0].Value.pbData = pbEncodedSAN;
    
    LPSTR szEkuOids[] = { szOID_PKIX_KP_SERVER_AUTH };
    CERT_ENHKEY_USAGE eku = { 1, szEkuOids };
    BYTE* pbEkuEncoded = NULL; DWORD cbEkuEncoded = 0;
    CryptEncodeObjectEx(X509_ASN_ENCODING, X509_ENHANCED_KEY_USAGE, &eku, CRYPT_ENCODE_ALLOC_FLAG, NULL, &pbEkuEncoded, &cbEkuEncoded);
    extArray[1].pszObjId = szOID_ENHANCED_KEY_USAGE;
    extArray[1].fCritical = FALSE;
    extArray[1].Value.cbData = cbEkuEncoded;
    extArray[1].Value.pbData = pbEkuEncoded;

    CERT_INFO userInfo = {0};
    userInfo.dwVersion = CERT_V3;
    BYTE sn[4]; CryptGenRandom(hUserProv, sizeof(sn), sn); sn[0] &= 0x7F;
    userInfo.SerialNumber.cbData = sizeof(sn); userInfo.SerialNumber.pbData = sn;
    userInfo.SignatureAlgorithm = sigAlg;
    userInfo.Issuer = pCACert->pCertInfo->Subject;
    userInfo.Subject = userNameBlob;
    userInfo.SubjectPublicKeyInfo = *pUserPubKey;
    userInfo.cExtension = 2;
    userInfo.rgExtension = extArray;
    
    SYSTEMTIME st; GetSystemTime(&st);
    SystemTimeToFileTime(&st, &userInfo.NotBefore);
    st.wYear += 10; SystemTimeToFileTime(&st, &userInfo.NotAfter);

    DWORD cbUserCert = 0;
    CryptSignAndEncodeCertificate(hCAProv, AT_SIGNATURE, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, X509_CERT_TO_BE_SIGNED, &userInfo, &sigAlg, NULL, NULL, &cbUserCert);
    pbUserCert = (BYTE*)malloc(cbUserCert);
    CryptSignAndEncodeCertificate(hCAProv, AT_SIGNATURE, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, X509_CERT_TO_BE_SIGNED, &userInfo, &sigAlg, NULL, pbUserCert, &cbUserCert);

    pUserCert = CertCreateCertificateContext(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, pbUserCert, cbUserCert);

    CRYPT_KEY_PROV_INFO userKeyInfo = {0};
    userKeyInfo.pwszContainerName = (LPWSTR)USER_CONTAINER_W;
    userKeyInfo.pwszProvName = (LPWSTR)MS_ENH_RSA_AES_PROV_W;
    userKeyInfo.dwProvType = PROV_RSA_AES;
    userKeyInfo.dwKeySpec = AT_KEYEXCHANGE;
    CertSetCertificateContextProperty(pUserCert, CERT_KEY_PROV_INFO_PROP_ID, 0, &userKeyInfo);

    if (pCACert) CertFreeCertificateContext(pCACert);
    free(caNameBlob.pbData); free(userNameBlob.pbData); free(pUserPubKey);
    free(pbUserCert); LocalFree(pbEncodedSAN); LocalFree(pbEkuEncoded);
    if (hCAKey) CryptDestroyKey(hCAKey);
    if (hUserKey) CryptDestroyKey(hUserKey);
    if (hCAProv) CryptReleaseContext(hCAProv, 0);
    if (hUserProv) CryptReleaseContext(hUserProv, 0);

    return pUserCert;
}

BOOL InitTLS(void) {
    if (!g_bUseTls) return TRUE;
    
    g_pTlsCert = GenerateCertificate();
    if (!g_pTlsCert) return FALSE;

    SCHANNEL_CRED SchannelCred = {0};
    SchannelCred.dwVersion = SCHANNEL_CRED_VERSION;
    SchannelCred.cCreds = 1;
    SchannelCred.paCred = &g_pTlsCert;
    SchannelCred.grbitEnabledProtocols = SP_PROT_TLS1_2_SERVER | SP_PROT_TLS1_3_SERVER;
    SchannelCred.dwFlags = SCH_CRED_NO_SYSTEM_MAPPER | SCH_CRED_IGNORE_NO_REVOCATION_CHECK | SCH_CRED_IGNORE_REVOCATION_OFFLINE;
    
    SECURITY_STATUS ss = AcquireCredentialsHandle(NULL, UNISP_NAME, SECPKG_CRED_INBOUND, NULL, &SchannelCred, NULL, NULL, &g_SrvCreds, NULL);
    
    if (ss == SEC_E_OK) {
        g_TlsInitialized = TRUE;
        return TRUE;
    } else {
        CertFreeCertificateContext(g_pTlsCert);
        g_pTlsCert = NULL;
        return FALSE;
    }
}

VOID CleanupTLS(void) {
    if (g_TlsInitialized) {
        FreeCredentialsHandle(&g_SrvCreds);
        g_TlsInitialized = FALSE;
    }
    if (g_pTlsCert) {
        CertFreeCertificateContext(g_pTlsCert);
        g_pTlsCert = NULL;
    }
}

VOID PopulateUserList(HWND listbox) {
    char keys[4096] = {0};
    SendMessage(listbox, LB_RESETCONTENT, 0, 0);
    GetPrivateProfileString("users", NULL, "", keys, sizeof(keys), CONFIG_FILE);
    char* p = keys;
    while (*p) {
        SendMessage(listbox, LB_ADDSTRING, 0, (LPARAM)p);
        p += strlen(p) + 1;
    }
}
BOOL TlsAccept(ClientThreadCtx* ctx) {
    if (!ctx->use_tls) return TRUE;
    
    SecBufferDesc inBufDesc, outBufDesc;
    SecBuffer inBufs[2], outBufs[1];
    DWORD flags = ASC_REQ_ALLOCATE_MEMORY | ASC_REQ_CONFIDENTIALITY | ASC_REQ_SEQUENCE_DETECT | ASC_REQ_REPLAY_DETECT | ASC_REQ_STREAM;
    DWORD outFlags;
    TimeStamp ts;
    SECURITY_STATUS status = SEC_E_INCOMPLETE_MESSAGE;
    BOOL init = TRUE;
    
    ctx->io_buf = malloc(65536);
    if (!ctx->io_buf) return FALSE;
    ctx->io_len = 0;
    
    while (TRUE) {
        if (ctx->io_len == 0 || status == SEC_E_INCOMPLETE_MESSAGE) {
            int r = recv(ctx->socket, ctx->io_buf + ctx->io_len, 65536 - ctx->io_len, 0);
            if (r <= 0) return FALSE;
            ctx->io_len += r;
        }
        
        inBufs[0].pvBuffer = ctx->io_buf;
        inBufs[0].cbBuffer = ctx->io_len;
        inBufs[0].BufferType = SECBUFFER_TOKEN;
        inBufs[1].BufferType = SECBUFFER_EMPTY;
        inBufDesc.cBuffers = 2; inBufDesc.pBuffers = inBufs; inBufDesc.ulVersion = SECBUFFER_VERSION;
        
        outBufs[0].pvBuffer = NULL; outBufs[0].BufferType = SECBUFFER_TOKEN; outBufs[0].cbBuffer = 0;
        outBufDesc.cBuffers = 1; outBufDesc.pBuffers = outBufs; outBufDesc.ulVersion = SECBUFFER_VERSION;
        
        status = AcceptSecurityContext(&g_SrvCreds, init ? NULL : &ctx->hCtx, &inBufDesc, flags, 0, &ctx->hCtx, &outBufDesc, &outFlags, &ts);
        
        if (outBufs[0].cbBuffer > 0 && outBufs[0].pvBuffer) {
            send(ctx->socket, outBufs[0].pvBuffer, outBufs[0].cbBuffer, 0);
            FreeContextBuffer(outBufs[0].pvBuffer);
        }
        
        if (status != SEC_E_OK && status != SEC_I_CONTINUE_NEEDED && status != SEC_E_INCOMPLETE_MESSAGE) {
            ctx->hCtx.dwLower = 0;
            ctx->hCtx.dwUpper = 0;
        }
        
        if (status == SEC_I_CONTINUE_NEEDED || status == SEC_E_OK) {
            init = FALSE;
        }
        
        if (status == SEC_E_OK) {
            QueryContextAttributes(&ctx->hCtx, SECPKG_ATTR_STREAM_SIZES, &ctx->sizes);
            if (inBufs[1].BufferType == SECBUFFER_EXTRA) {
                memmove(ctx->io_buf, ctx->io_buf + (ctx->io_len - inBufs[1].cbBuffer), inBufs[1].cbBuffer);
                ctx->io_len = inBufs[1].cbBuffer;
            } else { ctx->io_len = 0; }
            return TRUE;
        } else if (status == SEC_I_CONTINUE_NEEDED) {
            if (inBufs[1].BufferType == SECBUFFER_EXTRA) {
                memmove(ctx->io_buf, ctx->io_buf + (ctx->io_len - inBufs[1].cbBuffer), inBufs[1].cbBuffer);
                ctx->io_len = inBufs[1].cbBuffer;
            } else { ctx->io_len = 0; }
        } else if (status != SEC_E_INCOMPLETE_MESSAGE) {
            return FALSE;
        }
    }
}

int TlsRecv(ClientThreadCtx* ctx, char* out_buf, int max_len) {
    if (!ctx->use_tls) return recv(ctx->socket, out_buf, max_len, 0);
    
    SecBufferDesc msgDesc;
    SecBuffer bufs[4];
    SECURITY_STATUS status;
    
    while (TRUE) {
        if (ctx->io_len > 0) {
            bufs[0].pvBuffer = ctx->io_buf;
            bufs[0].cbBuffer = ctx->io_len;
            bufs[0].BufferType = SECBUFFER_DATA;
            bufs[1].BufferType = SECBUFFER_EMPTY;
            bufs[2].BufferType = SECBUFFER_EMPTY;
            bufs[3].BufferType = SECBUFFER_EMPTY;
            
            msgDesc.ulVersion = SECBUFFER_VERSION;
            msgDesc.cBuffers = 4;
            msgDesc.pBuffers = bufs;
            
            status = DecryptMessage(&ctx->hCtx, &msgDesc, 0, NULL);
            
            if (status == SEC_E_OK) {
                SecBuffer *pData = NULL, *pExtra = NULL;
                for (int i = 0; i < 4; i++) {
                    if (bufs[i].BufferType == SECBUFFER_DATA) pData = &bufs[i];
                    if (bufs[i].BufferType == SECBUFFER_EXTRA) pExtra = &bufs[i];
                }
                
                int copyLen = 0;
                if (pData && pData->cbBuffer > 0) {
                    copyLen = pData->cbBuffer > max_len ? max_len : pData->cbBuffer;
                    memcpy(out_buf, pData->pvBuffer, copyLen);
                }
                
                if (pExtra && pExtra->cbBuffer > 0) {
                    memmove(ctx->io_buf, ctx->io_buf + (ctx->io_len - pExtra->cbBuffer), pExtra->cbBuffer);
                    ctx->io_len = pExtra->cbBuffer;
                } else {
                    ctx->io_len = 0;
                }
                
                if (copyLen == 0) continue;
                return copyLen;
            } else if (status == SEC_I_CONTEXT_EXPIRED) {
                return 0; 
            } else if (status == SEC_I_RENEGOTIATE) {
                return -1;
            } else if (status != SEC_E_INCOMPLETE_MESSAGE) {
                return -1; 
            }
        }
        
        if (ctx->io_len >= 65536) return -1;
        
        int r = recv(ctx->socket, ctx->io_buf + ctx->io_len, 65536 - ctx->io_len, 0);
        if (r <= 0) return r;
        ctx->io_len += r;
    }
}

int TlsSend(ClientThreadCtx* ctx, const char* data, int len) {
    if (!ctx->use_tls) return send(ctx->socket, data, len, 0);
    if (len <= 0) return 0;
    
    int totalSent = 0;
    while (totalSent < len) {
        int chunkLen = len - totalSent;
        if (chunkLen > (int)ctx->sizes.cbMaximumMessage) {
            chunkLen = ctx->sizes.cbMaximumMessage;
        }
        
        char* pkt = malloc(ctx->sizes.cbHeader + chunkLen + ctx->sizes.cbTrailer);
        if (!pkt) return -1;
        
        memcpy(pkt + ctx->sizes.cbHeader, data + totalSent, chunkLen);
        
        SecBuffer bufs[4];
        bufs[0].pvBuffer = pkt; 
        bufs[0].cbBuffer = ctx->sizes.cbHeader; 
        bufs[0].BufferType = SECBUFFER_STREAM_HEADER;
        
        bufs[1].pvBuffer = pkt + ctx->sizes.cbHeader; 
        bufs[1].cbBuffer = chunkLen; 
        bufs[1].BufferType = SECBUFFER_DATA;
        
        bufs[2].pvBuffer = pkt + ctx->sizes.cbHeader + chunkLen; 
        bufs[2].cbBuffer = ctx->sizes.cbTrailer; 
        bufs[2].BufferType = SECBUFFER_STREAM_TRAILER;
        
        bufs[3].BufferType = SECBUFFER_EMPTY;
        
        SecBufferDesc desc = { SECBUFFER_VERSION, 4, bufs };
        if (EncryptMessage(&ctx->hCtx, 0, &desc, 0) != SEC_E_OK) {
            free(pkt);
            return -1;
        }
        
        int total = bufs[0].cbBuffer + bufs[1].cbBuffer + bufs[2].cbBuffer;
        int r = send(ctx->socket, pkt, total, 0);
        free(pkt);
        
        if (r != total) return -1;
        totalSent += chunkLen;
    }
    return totalSent;
}

VOID GenerateSaltedHash(const char* password, const char* salt, char* outHex) {
    HCRYPTPROV hProv;
    HCRYPTHASH hHash;
    outHex[0] = '\0';
    if (CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        if (CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
            CryptHashData(hHash, (const BYTE*)password, (DWORD)strlen(password), 0);
            CryptHashData(hHash, (const BYTE*)salt, (DWORD)strlen(salt), 0);
            BYTE hashBytes[32];
            DWORD hashLen = 32;
            if (CryptGetHashParam(hHash, HP_HASHVAL, hashBytes, &hashLen, 0)) {
                for (DWORD i = 0; i < hashLen; i++) snprintf(outHex + (i * 2), 3, "%02x", hashBytes[i]);
            }
            CryptDestroyHash(hHash);
        }
        CryptReleaseContext(hProv, 0);
    }
}

VOID Base64Decode(const char* input, char* output, int maxLen) {
    int decoding_table[256];
    const char* b64chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (int i = 0; i < 256; i++) decoding_table[i] = -1;
    for (int i = 0; i < 64; i++) decoding_table[(unsigned char)b64chars[i]] = i;
    int out_idx = 0, val = 0, valb = -8;
    for (const char* c = input; *c; c++) {
        if (decoding_table[(unsigned char)*c] == -1) break;
        val = (val << 6) + decoding_table[(unsigned char)*c];
        valb += 6;
        if (valb >= 0) {
            if (out_idx < maxLen - 1) output[out_idx++] = (char)((val >> valb) & 0xFF);
            valb -= 8;
        }
    }
    output[out_idx] = '\0';
}

BOOL AuthenticateRequest(char* buffer, char* outUser) {
    if (outUser) outUser[0] = '\0';
    
    char* authHeader = strstr(buffer, "Authorization: Basic ");
    if (!authHeader) {
        if (g_bAnonymous) {
            if (outUser) strcpy(outUser, "anonymous");
            return TRUE;
        }
        return FALSE;
    }
    authHeader += 21;
    char b64[256] = {0};
    int i = 0;
    while (authHeader[i] && authHeader[i] != '\r' && authHeader[i] != '\n' && authHeader[i] != ' ' && i < 255) b64[i++] = authHeader[i];
    b64[i] = '\0';
    char decoded[256] = {0};
    Base64Decode(b64, decoded, 256);
    char* colon = strchr(decoded, ':');
    if (!colon) return FALSE;
    *colon = '\0';
    char* username = decoded;
    char* password = colon + 1;
    
    char userEntry[256];
    GetPrivateProfileString("users", username, "", userEntry, sizeof(userEntry), CONFIG_FILE);
    if (strlen(userEntry) == 0) return FALSE;
    char* comma = strchr(userEntry, ',');
    if (!comma) return FALSE;
    *comma = '\0';
    
    char actualHash[65] = {0};
    GenerateSaltedHash(password, userEntry, actualHash);
    
    if (strcmp(comma + 1, actualHash) == 0) {
        if (outUser) strcpy(outUser, username);
        return TRUE;
    }
    return FALSE;
}

VOID SeedDefaultUser(VOID) {
    char buf[2];
    if (GetPrivateProfileString("users", "admin", "", buf, sizeof(buf), CONFIG_FILE) == 0) {
        char hash[65];
        GenerateSaltedHash("admin", "salt2026", hash);
        char entry[128];
        snprintf(entry, sizeof(entry), "salt2026,%s", hash);
        WritePrivateProfileString("users", "admin", entry, CONFIG_FILE);
        WritePrivateProfileString("perms", "admin", "1", CONFIG_FILE);
    }
}

BOOL LoadConfig(void) {
    char buffer[64];
    if (GetPrivateProfileString("server", "port", "443", buffer, sizeof(buffer), CONFIG_FILE)) {
        g_Port = atoi(buffer);
        if (g_Port < 1 || g_Port > 65535) g_Port = DEFAULT_PORT;
    }
    if (GetPrivateProfileString("server", "http_port", "8443", buffer, sizeof(buffer), CONFIG_FILE)) {
        g_HttpPort = atoi(buffer);
        if (g_HttpPort < 1 || g_HttpPort > 65535) g_HttpPort = DEFAULT_HTTP_PORT;
    }
    if (GetPrivateProfileString("server", "timeout_ms", "2000", buffer, sizeof(buffer), CONFIG_FILE)) {
        g_TimeoutMs = atoi(buffer);
        if (g_TimeoutMs < 1) g_TimeoutMs = DEFAULT_TIMEOUT_MS;
    }
    if (GetPrivateProfileString("server", "anonymous", "1", buffer, sizeof(buffer), CONFIG_FILE)) g_bAnonymous = (atoi(buffer) != 0);
    if (GetPrivateProfileString("server", "use_tls", "1", buffer, sizeof(buffer), CONFIG_FILE)) g_bUseTls = (atoi(buffer) != 0);
    return TRUE;
}

BOOL SaveConfig(void) {
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%d", g_Port); WritePrivateProfileString("server", "port", buffer, CONFIG_FILE);
    snprintf(buffer, sizeof(buffer), "%d", g_HttpPort); WritePrivateProfileString("server", "http_port", buffer, CONFIG_FILE);
    snprintf(buffer, sizeof(buffer), "%d", g_TimeoutMs); WritePrivateProfileString("server", "timeout_ms", buffer, CONFIG_FILE);
    snprintf(buffer, sizeof(buffer), "%d", g_bAnonymous ? 1 : 0); WritePrivateProfileString("server", "anonymous", buffer, CONFIG_FILE);
    snprintf(buffer, sizeof(buffer), "%d", g_bUseTls ? 1 : 0); WritePrivateProfileString("server", "use_tls", buffer, CONFIG_FILE);
    return TRUE;
}

BOOL StartHttpServer(int port) {
    SOCKADDR_IN addr;
    g_HttpListenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_HttpListenSocket == INVALID_SOCKET) return FALSE;
    char optval = 1;
    setsockopt(g_HttpListenSocket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (bind(g_HttpListenSocket, (SOCKADDR*)&addr, sizeof(addr)) == SOCKET_ERROR || listen(g_HttpListenSocket, MAX_CONNECTIONS) == SOCKET_ERROR) {
        closesocket(g_HttpListenSocket); g_HttpListenSocket = INVALID_SOCKET; return FALSE;
    }
    return TRUE;
}

VOID StopHttpServer(VOID) {
    if (g_HttpListenSocket != INVALID_SOCKET) { closesocket(g_HttpListenSocket); g_HttpListenSocket = INVALID_SOCKET; }
}

unsigned __stdcall HttpServerThread(void* pParam) {
    (void)pParam;
    while (g_bRunning && g_HttpListenSocket != INVALID_SOCKET) {
        SOCKADDR_IN clientAddr;
        int clientAddrLen = sizeof(clientAddr);
        SOCKET clientSocket = accept(g_HttpListenSocket, (SOCKADDR*)&clientAddr, &clientAddrLen);
        if (clientSocket == INVALID_SOCKET) continue;
        ClientThreadCtx* ctx = calloc(1, sizeof(ClientThreadCtx));
        if (ctx) {
            ctx->socket = clientSocket;
            ctx->port = g_HttpPort;
            ctx->use_tls = g_bUseTls;
            HANDLE hThread = (HANDLE)_beginthreadex(NULL, 0, HttpClientHandler, ctx, 0, NULL);
            if (hThread) CloseHandle(hThread);
            else { free(ctx); closesocket(clientSocket); }
        } else closesocket(clientSocket);
    }
    return 0;
}

unsigned __stdcall HttpClientHandler(void* pParam) {
    ClientThreadCtx* ctx = (ClientThreadCtx*)pParam;
    if (!ctx) return 0;

    DWORD timeout = (DWORD)g_TimeoutMs;
    setsockopt(ctx->socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    setsockopt(ctx->socket, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
    
    BOOL clean_shutdown = FALSE;
    if (!TlsAccept(ctx)) goto cleanup;
    
    char buffer[BUFFER_SIZE];
    char responseBuf[BUFFER_SIZE * 2];
    memset(buffer, 0, sizeof(buffer));
    
    int bytesReceived = TlsRecv(ctx, buffer, sizeof(buffer) - 1);
    clean_shutdown = (bytesReceived >= 0);
    
    if (bytesReceived > 0) {
        buffer[bytesReceived] = '\0';
        char method[16] = {0};
        char path[MAX_PATH] = {0};
        GetHTTPMethod(buffer, method, sizeof(method));
        GetRequestPath(buffer, path, sizeof(path));
        
        if (method[0] != '\0') {
            if (ctx->port == g_HttpPort && strcmp(method, "GET") == 0 && strncmp(path, "/calendar/", 10) != 0 && strncmp(path, "/api/", 5) != 0 && (strcmp(path, "/") == 0 || strstr(path, ".html") || strstr(path, ".css") || strstr(path, ".js") || strstr(path, ".json"))) {
                if (strcmp(path, "/") == 0) strncpy(path, "/calendar.html", sizeof(path) - 1);
                char fullPath[MAX_PATH];
                snprintf(fullPath, sizeof(fullPath), "%s%s", WEB_DIR, path);
                if (strstr(path, "..") == NULL) SendHTTPFileResponse(ctx, fullPath);
                else SendHTTPResponse(ctx, "403 Forbidden", "text/plain", "Forbidden", 9, NULL);
            } 
            else {
                char authUser[256] = {0};
                if (strcmp(method, "OPTIONS") == 0 || AuthenticateRequest(buffer, authUser)) {
                    HandleCommandLine(ctx, buffer, responseBuf, authUser); 
                } else {
                    SendHTTPResponse(ctx, "401 Unauthorized", "text/plain", "Unauthorized", 12, "WWW-Authenticate: x-Basic realm=\"CalDAV Server\"\r\n");
                }
            }
        }
    }

cleanup:
    if (ctx->use_tls) {
        if (clean_shutdown && ctx->sizes.cbHeader > 0 && (ctx->hCtx.dwLower != 0 || ctx->hCtx.dwUpper != 0)) {
            DWORD dwType = SCHANNEL_SHUTDOWN;
            SecBuffer outBuffers[1];
            outBuffers[0].pvBuffer = &dwType;
            outBuffers[0].BufferType = SECBUFFER_TOKEN;
            outBuffers[0].cbBuffer = sizeof(dwType);
            SecBufferDesc outBufferDesc = { SECBUFFER_VERSION, 1, outBuffers };
            
            if (ApplyControlToken(&ctx->hCtx, &outBufferDesc) == SEC_E_OK) {
                SecBuffer closeBufs[1];
                closeBufs[0].pvBuffer = NULL;
                closeBufs[0].BufferType = SECBUFFER_TOKEN;
                closeBufs[0].cbBuffer = 0;
                SecBufferDesc closeDesc = { SECBUFFER_VERSION, 1, closeBufs };
                DWORD outFlags;
                TimeStamp ts;
                
                DWORD flags = ASC_REQ_ALLOCATE_MEMORY | ASC_REQ_CONFIDENTIALITY | ASC_REQ_SEQUENCE_DETECT | ASC_REQ_REPLAY_DETECT | ASC_REQ_STREAM;
                SECURITY_STATUS closeStatus = AcceptSecurityContext(&g_SrvCreds, &ctx->hCtx, NULL, flags, 0, &ctx->hCtx, &closeDesc, &outFlags, &ts);
                
                if (closeStatus == SEC_E_OK || closeStatus == SEC_I_CONTEXT_EXPIRED) {
                    if (closeBufs[0].pvBuffer && closeBufs[0].cbBuffer > 0) {
                        send(ctx->socket, closeBufs[0].pvBuffer, closeBufs[0].cbBuffer, 0);
                        FreeContextBuffer(closeBufs[0].pvBuffer);
                    }
                } else {
                    ctx->hCtx.dwLower = 0;
                    ctx->hCtx.dwUpper = 0;
                }
            }
        }
        
        if (ctx->hCtx.dwLower != 0 || ctx->hCtx.dwUpper != 0) {
            DeleteSecurityContext(&ctx->hCtx);
        }
        if (ctx->io_buf) free(ctx->io_buf);
    }
    
    shutdown(ctx->socket, SD_BOTH);
    closesocket(ctx->socket);
    free(ctx);
    return 0;
}

VOID SendHTTPFileResponse(ClientThreadCtx* ctx, const char* filePath) {
    FILE* fp = fopen(filePath, "rb");
    if (!fp) {
        SendHTTPResponse(ctx, "404 Not Found", "text/plain", "File Not Found", 14, NULL);
        return;
    }
    fseek(fp, 0, SEEK_END);
    long fileSize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    const char* contentType = "application/octet-stream";
    if (strstr(filePath, ".html")) contentType = "text/html";
    else if (strstr(filePath, ".css")) contentType = "text/css";
    else if (strstr(filePath, ".js")) contentType = "application/javascript";
    else if (strstr(filePath, ".json")) contentType = "application/json";
    
    char header[512];
    int headerLen = snprintf(header, sizeof(header), 
        "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %ld\r\nConnection: close\r\n"
        "Cache-Control: no-store, no-cache, must-revalidate\r\n\r\n", contentType, fileSize);
    TlsSend(ctx, header, headerLen);
    
    char fileBuf[4096];
    size_t bytesRead;
    while ((bytesRead = fread(fileBuf, 1, sizeof(fileBuf), fp)) > 0) {
        TlsSend(ctx, fileBuf, (int)bytesRead);
    }
    fclose(fp);
}

BOOL StartServer(int port) {
    WSADATA wsaData;
    SOCKADDR_IN addr;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return FALSE;
    g_ListenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_ListenSocket == INVALID_SOCKET) { WSACleanup(); return FALSE; }
    char optval = 1;
    setsockopt(g_ListenSocket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (bind(g_ListenSocket, (SOCKADDR*)&addr, sizeof(addr)) == SOCKET_ERROR || listen(g_ListenSocket, MAX_CONNECTIONS) == SOCKET_ERROR) {
        closesocket(g_ListenSocket); WSACleanup(); return FALSE;
    }
    return TRUE;
}

void StopServer(VOID) {
    if (g_ListenSocket != INVALID_SOCKET) { closesocket(g_ListenSocket); g_ListenSocket = INVALID_SOCKET; }
    StopHttpServer();
    WSACleanup();
}

unsigned __stdcall ServerThread(void* pParam) {
    (void)pParam;
    while (g_bRunning && g_ListenSocket != INVALID_SOCKET) {
        SOCKADDR_IN clientAddr;
        int clientAddrLen = sizeof(clientAddr);
        SOCKET clientSocket = accept(g_ListenSocket, (SOCKADDR*)&clientAddr, &clientAddrLen);
        if (clientSocket == INVALID_SOCKET) continue;
        ClientThreadCtx* ctx = calloc(1, sizeof(ClientThreadCtx));
        if (ctx) {
            ctx->socket = clientSocket;
            ctx->port = g_Port;
            ctx->use_tls = g_bUseTls;
            HANDLE hThread = (HANDLE)_beginthreadex(NULL, 0, ClientHandler, ctx, 0, NULL);
            if (hThread) CloseHandle(hThread);
            else { free(ctx); closesocket(clientSocket); }
        } else closesocket(clientSocket);
    }
    return 0;
}

void GetHTTPMethod(char* buffer, char* outMethod, int maxLen) {
    outMethod[0] = '\0';
    char* space = strchr(buffer, ' ');
    if (!space) return;
    int len = (int)(space - buffer);
    if (len >= maxLen) len = maxLen - 1;
    strncpy_s(outMethod, maxLen, buffer, len);
    outMethod[len] = '\0';
}

void GetRequestPath(char* buffer, char* outPath, int maxLen) {
    outPath[0] = '\0';
    char* space1 = strchr(buffer, ' ');
    if (!space1) {
        strncpy(outPath, "/", maxLen - 1);
        outPath[maxLen - 1] = '\0';
        return;
    }
    space1++;
    char* space2 = strchr(space1, ' ');
    int len = space2 ? (int)(space2 - space1) : (int)strlen(space1);
    if (len >= maxLen) len = maxLen - 1;
    strncpy(outPath, space1, len);
    outPath[len] = '\0';
    char* query = strchr(outPath, '?');
    if (query) *query = '\0';
}

VOID SendHTTPResponse(ClientThreadCtx* ctx, const char* status, const char* contentType, const char* body, SIZE_T bodyLen, const char* extraHeaders) {
    char header[1024];
    int headerLen = snprintf(header, sizeof(header), 
        "HTTP/1.1 %s\r\nContent-Type: %s; charset=utf-8\r\nContent-Length: %lu\r\nConnection: close\r\n"
        "Access-Control-Allow-Origin: *\r\nAccess-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type, Authorization\r\nServer: %s/%s\r\n%s\r\n",
        status, contentType, (unsigned long)bodyLen, SERVER_NAME, SERVER_VERSION, extraHeaders ? extraHeaders : "");
    TlsSend(ctx, header, headerLen);
    if (bodyLen > 0 && body) TlsSend(ctx, body, (int)bodyLen);
}

unsigned __stdcall ClientHandler(void* pParam) {
    return HttpClientHandler(pParam);
}

unsigned int CalculateEventETag(const char* title, const char* date, int startMin, int duration, const char* rrule) {
    unsigned int hash = 5381;
    const char* str = title; while (*str) hash = ((hash << 5) + hash) + *str++;
    str = date; while (*str) hash = ((hash << 5) + hash) + *str++;
    str = rrule; if (str) while (*str) hash = ((hash << 5) + hash) + *str++;
    hash = ((hash << 5) + hash) + startMin;
    hash = ((hash << 5) + hash) + duration;
    return hash;
}

VOID GetCalendarCTag(char* outCtag, int maxLen) {
    WIN32_FIND_DATA fd;
    char searchPath[MAX_PATH];
    snprintf(searchPath, sizeof(searchPath), "%s\\*.json", DATA_DIR);
    HANDLE hFind = FindFirstFile(searchPath, &fd);
    
    FILETIME maxTime = {0};
    
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (CompareFileTime(&fd.ftLastWriteTime, &maxTime) > 0) {
                maxTime = fd.ftLastWriteTime;
            }
        } while (FindNextFile(hFind, &fd));
        FindClose(hFind);
    }
    snprintf(outCtag, maxLen, "%lu-%lu", maxTime.dwHighDateTime, maxTime.dwLowDateTime);
}

int GetUserPersonIdx(const char* username) {
    if (!username || !username[0]) return 0;
    int idx = 0, curr = 0;
    char filepath[MAX_PATH];
    snprintf(filepath, sizeof(filepath), "%s/people.json", DATA_DIR);
    FILE* fp = fopen(filepath, "rb");
    if (fp) {
        char buf[4096];
        size_t bytes = fread(buf, 1, sizeof(buf)-1, fp);
        buf[bytes] = '\0';
        
        char searchStr[256];
        snprintf(searchStr, sizeof(searchStr), "\"%s\"", username);
        char* p = buf;
        
        while (*p) {
            if (*p == '"') {
                if (strncmp(p, searchStr, strlen(searchStr)) == 0) { idx = curr; break; }
                p++; while (*p && *p != '"') p++;
                curr++;
            }
            if (*p) p++;
        }
        fclose(fp);
    }
    return idx;
}

VOID HandleCommandLine(ClientThreadCtx* ctx, char* buffer, char* response, const char* authUser) {
    char method[16] = {0};
    char path[MAX_PATH] = {0};
    GetHTTPMethod(buffer, method, sizeof(method));
    GetRequestPath(buffer, path, sizeof(path));
    
    if (strstr(path, "..")) {
        SendHTTPResponse(ctx, "403 Forbidden", "text/plain", "Forbidden", 9, NULL);
        return;
    }
    
    char* body = strstr(buffer, "\r\n\r\n");
    if (body) body += 4;
    
    int contentLen = 0;
    char* clPtr = strstr(buffer, "Content-Length:");
    if (!clPtr) clPtr = strstr(buffer, "Content-length:");
    if (clPtr) contentLen = atoi(clPtr + 15);

    if (strcmp(method, "OPTIONS") == 0) {
        SendHTTPResponse(ctx, "200 OK", "text/plain", "", 0, 
            "DAV: 1, 2, 3, calendar-access\r\n"
            "Allow: OPTIONS, GET, HEAD, POST, PUT, DELETE, TRACE, COPY, MOVE, PROPFIND, PROPPATCH, REPORT\r\n");
    }
    else if (strcmp(method, "PROPFIND") == 0) {
        char xmlResponse[4096];
        
        if (strncmp(path, "/principal/", 11) == 0) {
            snprintf(xmlResponse, sizeof(xmlResponse),
                "<?xml version=\"1.0\" encoding=\"utf-8\" ?>\n"
                "<D:multistatus xmlns:D=\"DAV:\" xmlns:C=\"urn:ietf:params:xml:ns:caldav\">\n"
                "  <D:response>\n"
                "    <D:href>%s</D:href>\n"
                "    <D:propstat>\n"
                "      <D:prop>\n"
                "        <D:resourcetype><D:principal/></D:resourcetype>\n"
                "        <D:displayname>%s</D:displayname>\n"
                "        <C:calendar-home-set><D:href>/calendar/</D:href></C:calendar-home-set>\n"
                "      </D:prop>\n"
                "      <D:status>HTTP/1.1 200 OK</D:status>\n"
                "    </D:propstat>\n"
                "  </D:response>\n"
                "</D:multistatus>\n",
                path, (authUser && authUser[0]) ? authUser : "anonymous");
        } else {
            char ctag[64] = {0}, calName[128] = "CalDAV-D Server", calColor[32] = "#039BE5";
            GetCalendarCTag(ctag, sizeof(ctag));
            GetPrivateProfileString("calendar_props", "displayname", "CalDAV-D Server", calName, sizeof(calName), CONFIG_FILE);
            GetPrivateProfileString("calendar_props", "color", "#039BE5", calColor, sizeof(calColor), CONFIG_FILE);
            
            snprintf(xmlResponse, sizeof(xmlResponse),
                "<?xml version=\"1.0\" encoding=\"utf-8\" ?>\n"
                "<D:multistatus xmlns:D=\"DAV:\" xmlns:C=\"urn:ietf:params:xml:ns:caldav\" xmlns:CS=\"http://calendarserver.org/ns/\" xmlns:ICAL=\"http://apple.com/ns/ical/\">\n"
                "  <D:response>\n"
                "    <D:href>%s</D:href>\n"
                "    <D:propstat>\n"
                "      <D:prop>\n"
                "        <D:resourcetype><D:collection/><C:calendar/></D:resourcetype>\n"
                "        <D:displayname>%s</D:displayname>\n"
                "        <ICAL:calendar-color>%s</ICAL:calendar-color>\n"
                "        <CS:getctag>\"%s\"</CS:getctag>\n"
                "        <D:current-user-principal><D:href>/principal/%s/</D:href></D:current-user-principal>\n"
                "        <C:calendar-home-set><D:href>/calendar/</D:href></C:calendar-home-set>\n"
                "      </D:prop>\n"
                "      <D:status>HTTP/1.1 200 OK</D:status>\n"
                "    </D:propstat>\n"
                "  </D:response>\n"
                "</D:multistatus>\n", 
                path, calName, calColor, ctag, (authUser && authUser[0]) ? authUser : "anonymous");
        }

        SendHTTPResponse(ctx, "207 Multi-Status", "application/xml; charset=utf-8", xmlResponse, strlen(xmlResponse), NULL);
    }
    else if (strcmp(method, "PROPPATCH") == 0) {
        char color[32] = {0}, name[128] = {0};
        char* pColor = strstr(body, "<calendar-color");
        if (!pColor) pColor = strstr(body, "<ICAL:calendar-color");
        if (pColor) {
            pColor = strchr(pColor, '>');
            if (pColor) {
                sscanf(pColor + 1, "%31[^<]", color);
                WritePrivateProfileString("calendar_props", "color", color, CONFIG_FILE);
            }
        }
        
        char* pName = strstr(body, "<D:displayname>");
        if (pName) {
            sscanf(pName + 15, "%127[^<]", name);
            WritePrivateProfileString("calendar_props", "displayname", name, CONFIG_FILE);
        }

        char xmlResponse[1024];
        snprintf(xmlResponse, sizeof(xmlResponse),
            "<?xml version=\"1.0\" encoding=\"utf-8\" ?>\n"
            "<D:multistatus xmlns:D=\"DAV:\">\n"
            "  <D:response>\n"
            "    <D:href>%s</D:href>\n"
            "    <D:propstat>\n"
            "      <D:prop><D:displayname/><calendar-color xmlns=\"http://apple.com/ns/ical/\"/></D:prop>\n"
            "      <D:status>HTTP/1.1 200 OK</D:status>\n"
            "    </D:propstat>\n"
            "  </D:response>\n"
            "</D:multistatus>\n", path);
        SendHTTPResponse(ctx, "207 Multi-Status", "application/xml; charset=utf-8", xmlResponse, strlen(xmlResponse), NULL);
    }
    else if (strcmp(method, "REPORT") == 0) {
        char currentCtag[64] = {0};
        GetCalendarCTag(currentCtag, sizeof(currentCtag));
        char currentToken[128];
        snprintf(currentToken, sizeof(currentToken), "http://caldavd/sync/%s", currentCtag);

        char clientToken[128] = {0};
        BOOL isSyncReport = body && strstr(body, "sync-collection") != NULL;
        BOOL isMultiget = body && (strstr(body, ":calendar-multiget") != NULL || strstr(body, "<calendar-multiget") != NULL);
        
        if (isSyncReport) {
            char* pToken = strstr(body, "sync-token>");
            if (pToken) {
                char* pEnd = strchr(pToken + 11, '<');
                if (pEnd) {
                    int len = pEnd - (pToken + 11);
                    if (len > 0 && len < 127) {
                        strncpy(clientToken, pToken + 11, len);
                        clientToken[len] = '\0';
                    }
                }
            }
            
            if (clientToken[0] != '\0') {
                if (strcmp(clientToken, currentToken) == 0) {
                    char xmlResponse[1024];
                    snprintf(xmlResponse, sizeof(xmlResponse),
                        "<?xml version=\"1.0\" encoding=\"utf-8\" ?>\n"
                        "<D:multistatus xmlns:D=\"DAV:\" xmlns:C=\"urn:ietf:params:xml:ns:caldav\">\n"
                        "  <D:sync-token>%s</D:sync-token>\n"
                        "</D:multistatus>\n", currentToken);
                    SendHTTPResponse(ctx, "207 Multi-Status", "application/xml; charset=utf-8", xmlResponse, strlen(xmlResponse), NULL);
                    return;
                } else {
                    const char* errResp = 
                        "<?xml version=\"1.0\" encoding=\"utf-8\" ?>\n"
                        "<D:error xmlns:D=\"DAV:\">\n"
                        "  <D:valid-sync-token/>\n"
                        "</D:error>\n";
                    SendHTTPResponse(ctx, "403 Forbidden", "application/xml; charset=utf-8", errResp, strlen(errResp), NULL);
                    return;
                }
            }
        }

        char requestedUids[4096] = "|";
        if (isMultiget && body) {
            char* href = body;
            char* hrefTag = strstr(href, "href>");
            while (hrefTag != NULL) {
                href = hrefTag + 5;
                char* endHref = strchr(href, '<');
                if (endHref) {
                    char hrefBuf[256] = {0};
                    int len = endHref - href;
                    if (len < 255) {
                        strncpy(hrefBuf, href, len);
                        char uid[128] = {0};
                        char* lastSlash = strrchr(hrefBuf, '/');
                        if (lastSlash) sscanf(lastSlash + 1, "%127[^.]", uid);
                        else sscanf(hrefBuf, "%127[^.]", uid);
                        
                        if (uid[0] != '\0' && strlen(requestedUids) + strlen(uid) + 2 < sizeof(requestedUids)) {
                            strcat(requestedUids, uid);
                            strcat(requestedUids, "|");
                        }
                    }
                }
                hrefTag = strstr(href, "href>");
            }
        }

        char* xmlResponse = (char*)malloc(1024 * 1024);
        if (!xmlResponse) {
            SendHTTPResponse(ctx, "500 Internal Error", "text/plain", "Memory Error", 12, NULL);
            return;
        }
        
        strcpy(xmlResponse, "<?xml version=\"1.0\" encoding=\"utf-8\" ?>\n<D:multistatus xmlns:D=\"DAV:\" xmlns:C=\"urn:ietf:params:xml:ns:caldav\">\n");

        WIN32_FIND_DATA fd;
        char searchPath[MAX_PATH];
        snprintf(searchPath, sizeof(searchPath), "%s\\*.json", DATA_DIR);
        HANDLE hFind = FindFirstFile(searchPath, &fd);
        
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (strcmp(fd.cFileName, "people.json") == 0 || strcmp(fd.cFileName, "contacts.json") == 0) continue;
                
                char filepath[MAX_PATH];
                snprintf(filepath, sizeof(filepath), "%s/%s", DATA_DIR, fd.cFileName);
                FILE* fp = fopen(filepath, "rb");
                if (fp) {
                    fseek(fp, 0, SEEK_END); long size = ftell(fp); fseek(fp, 0, SEEK_SET);
                    
                    char* fileBuf = malloc(size + 1);
                    if (fileBuf) {
                        fread(fileBuf, 1, size, fp); fileBuf[size] = '\0';
                        
                        char* eventStart = fileBuf;
                        while ((eventStart = strstr(eventStart, "{\"id\":")) != NULL) {
                            char id[64] = {0}, title[128] = "Untitled Event", date[16] = {0}, rrule[256] = {0};
                            int startMin = 0, duration = 60;
                            
                            char* pId = strstr(eventStart, "\"id\":\"");
                            if (pId) sscanf(pId + 6, "%63[^\"]", id);

                            if (isMultiget) {
                                char searchUid[130];
                                snprintf(searchUid, sizeof(searchUid), "|%s|", id);
                                if (strstr(requestedUids, searchUid) == NULL) {
                                    eventStart++;
                                    continue;
                                }
                            }
                            
                            char* pTitle = strstr(eventStart, "\"title\":\"");
                            if (pTitle) sscanf(pTitle + 9, "%127[^\"]", title);
                            
                            char* pDate = strstr(eventStart, "\"date\":\"");
                            if (pDate) sscanf(pDate + 8, "%15[^\"]", date);
                            
                            char* pStart = strstr(eventStart, "\"startMin\":");
                            if (pStart) sscanf(pStart + 11, "%d", &startMin);
                            
                            char* pDur = strstr(eventStart, "\"duration\":");
                            if (pDur) sscanf(pDur + 11, "%d", &duration);
                            
                            char* pRrule = strstr(eventStart, "\"rrule\":\"");
                            if (pRrule) sscanf(pRrule + 9, "%255[^\"]", rrule);
                            
                            unsigned int etagHash = CalculateEventETag(title, date, startMin, duration, rrule);
                            
                            char icsDate[9] = {0};
                            if (strlen(date) >= 10) {
                                icsDate[0] = date[0]; icsDate[1] = date[1]; icsDate[2] = date[2]; icsDate[3] = date[3];
                                icsDate[4] = date[5]; icsDate[5] = date[6];
                                icsDate[6] = date[8]; icsDate[7] = date[9];
                            }
                            
                            int startH = startMin / 60;
                            int startM = startMin % 60;
                            int endMin = startMin + duration;
                            int endH = endMin / 60;
                            int endM = endMin % 60;
                            
                            char rruleBlock[300] = {0};
                            if (rrule[0]) snprintf(rruleBlock, sizeof(rruleBlock), "RRULE:%s\r\n", rrule);
                            
                            char eventBuf[2048];
                            snprintf(eventBuf, sizeof(eventBuf),
                                "  <D:response>\n"
                                "    <D:href>/calendar/%s.ics</D:href>\n"
                                "    <D:propstat>\n"
                                "      <D:prop>\n"
                                "        <D:getetag>\"%s-%x\"</D:getetag>\n"
                                "        <C:calendar-data><![CDATA[BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//CalDAV-D//EN\r\nBEGIN:VEVENT\r\nUID:%s\r\nDTSTAMP:%sT%02d%02d00\r\nDTSTART:%sT%02d%02d00\r\nDTEND:%sT%02d%02d00\r\n%sSUMMARY:%s\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n]]></C:calendar-data>\n"
                                "      </D:prop>\n"
                                "      <D:status>HTTP/1.1 200 OK</D:status>\n"
                                "    </D:propstat>\n"
                                "  </D:response>\n",
                                id, id, etagHash, id, icsDate, startH, startM, icsDate, startH, startM, icsDate, endH, endM, rruleBlock, title);
                                
                            if (strlen(xmlResponse) + strlen(eventBuf) < 1024 * 1024 - 500) {
                                strcat(xmlResponse, eventBuf);
                            }
                            eventStart++;
                        }
                        free(fileBuf);
                    }
                    fclose(fp);
                }
            } while (FindNextFile(hFind, &fd));
            FindClose(hFind);
        }
        
        if (isSyncReport) {
            char tokenBlock[256];
            snprintf(tokenBlock, sizeof(tokenBlock), "  <D:sync-token>%s</D:sync-token>\n", currentToken);
            strcat(xmlResponse, tokenBlock);
        }
        
        strcat(xmlResponse, "</D:multistatus>\n");
        SendHTTPResponse(ctx, "207 Multi-Status", "application/xml; charset=utf-8", xmlResponse, strlen(xmlResponse), NULL);
        free(xmlResponse);
    }
    else if (strcmp(method, "GET") == 0) {
        if (strcmp(path, "/api/me") == 0) {
            char canEdit[8] = "0";
            GetPrivateProfileString("perms", authUser, "0", canEdit, sizeof(canEdit), CONFIG_FILE);
            char visible[1024] = "";
            char prefKey[256]; snprintf(prefKey, sizeof(prefKey), "%s_visible", authUser);
            GetPrivateProfileString("prefs", prefKey, "", visible, sizeof(visible), CONFIG_FILE);
            
            char json[2048];
            snprintf(json, sizeof(json), "{\"user\":\"%s\", \"edit_all\":%s, \"visible_people\":\"%s\"}", 
                authUser, canEdit[0]=='1'?"true":"false", visible);
            SendHTTPResponse(ctx, "200 OK", "application/json", json, strlen(json), NULL);
            return;
        }
        else if (strcmp(path, "/api/contacts") == 0) {
            char filepath[MAX_PATH];
            snprintf(filepath, sizeof(filepath), "%s/contacts.json", DATA_DIR);
            FILE* fp = fopen(filepath, "rb");
            if (fp) {
                fseek(fp, 0, SEEK_END); long size = ftell(fp); fseek(fp, 0, SEEK_SET);
                char* fileBuf = malloc(size + 1);
                if (fileBuf) {
                    fread(fileBuf, 1, size, fp); fileBuf[size] = '\0';
                    SendHTTPResponse(ctx, "200 OK", "application/json", fileBuf, size, NULL);
                    free(fileBuf);
                }
                fclose(fp);
            } else {
                SendHTTPResponse(ctx, "200 OK", "application/json", "[]", 2, NULL);
            }
            return;
        }
        else if (strcmp(path, "/calendar/all") == 0) {
            char* responseBody = (char*)malloc(1024 * 1024);
            if (responseBody) {
                strcpy(responseBody, "[");
                WIN32_FIND_DATA fd;
                char searchPath[MAX_PATH];
                snprintf(searchPath, sizeof(searchPath), "%s\\*.json", DATA_DIR);
                HANDLE hFind = FindFirstFile(searchPath, &fd);
                int first = 1;
                if (hFind != INVALID_HANDLE_VALUE) {
                    do {
                        if (strcmp(fd.cFileName, "people.json") == 0 || strcmp(fd.cFileName, "contacts.json") == 0) continue;
                        char filepath[MAX_PATH];
                        snprintf(filepath, sizeof(filepath), "%s/%s", DATA_DIR, fd.cFileName);
                        FILE* fp = fopen(filepath, "rb");
                        if (fp) {
                            fseek(fp, 0, SEEK_END);
                            long size = ftell(fp);
                            fseek(fp, 0, SEEK_SET);
                            if (size > 0 && strlen(responseBody) + size + 2 < 1024 * 1024) {
                                if (!first) strcat(responseBody, ",");
                                int curLen = strlen(responseBody);
                                fread(responseBody + curLen, 1, size, fp);
                                responseBody[curLen + size] = '\0';
                                first = 0;
                            }
                            fclose(fp);
                        }
                    } while (FindNextFile(hFind, &fd));
                    FindClose(hFind);
                }
                strcat(responseBody, "]");
                SendHTTPResponse(ctx, "200 OK", "application/json", responseBody, strlen(responseBody), NULL);
                free(responseBody);
            } else { SendHTTPResponse(ctx, "500 Internal Error", "text/plain", "Memory Error", 12, NULL); }
        }
        else if (strncmp(path, "/calendar/", 10) == 0) {
            char filepath[MAX_PATH];
            snprintf(filepath, sizeof(filepath), "%s/%s", DATA_DIR, path + 10);
            FILE* fp = fopen(filepath, "rb");
            if (fp) {
                fseek(fp, 0, SEEK_END);
                long size = ftell(fp);
                fseek(fp, 0, SEEK_SET);
                char* fileBuf = malloc(size + 1);
                if (fileBuf) {
                    fread(fileBuf, 1, size, fp);
                    fileBuf[size] = '\0';
                    SendHTTPResponse(ctx, "200 OK", "application/json", fileBuf, size, NULL);
                    free(fileBuf);
                }
                fclose(fp);
            } else { SendHTTPResponse(ctx, "404 Not Found", "text/plain", "Not Found", 9, NULL); }
        }
        else { SendHTTPResponse(ctx, "404 Not Found", "text/plain", "Endpoint not found", 18, NULL); }
    }
    else if (strcmp(method, "PUT") == 0) {
        if (strcmp(path, "/api/contacts") == 0) {
            if (body && contentLen > 0) {
                char filepath[MAX_PATH];
                snprintf(filepath, sizeof(filepath), "%s/contacts.json", DATA_DIR);
                FILE* fp = fopen(filepath, "wb");
                if (fp) {
                    fwrite(body, 1, contentLen, fp);
                    fclose(fp);
                }
            }
            SendHTTPResponse(ctx, "200 OK", "text/plain", "Saved", 5, NULL);
            return;
        }
        else if (strcmp(path, "/api/user") == 0) {
            if (body && contentLen > 0) {
                char* safeBody = malloc(contentLen + 1);
                memcpy(safeBody, body, contentLen);
                safeBody[contentLen] = '\0';
                
                char oldName[128] = {0}, newName[128] = {0};
                char* pipe = strchr(safeBody, '|');
                if (pipe) {
                    *pipe = '\0';
                    strncpy(oldName, safeBody, 127);
                    strncpy(newName, pipe + 1, 127);
                } else {
                    strncpy(newName, safeBody, 127);
                }
                free(safeBody);
                
                if (newName[0]) {
                    char existingUser[256] = {0};
                    if (oldName[0]) {
                        GetPrivateProfileString("users", oldName, "", existingUser, sizeof(existingUser), CONFIG_FILE);
                    }
                    
                    if (existingUser[0]) {
                        WritePrivateProfileString("users", newName, existingUser, CONFIG_FILE);
                        if (strcmp(oldName, newName) != 0) {
                            WritePrivateProfileString("users", oldName, NULL, CONFIG_FILE);
                            char perms[8] = "0";
                            GetPrivateProfileString("perms", oldName, "0", perms, sizeof(perms), CONFIG_FILE);
                            WritePrivateProfileString("perms", newName, perms, CONFIG_FILE);
                            WritePrivateProfileString("perms", oldName, NULL, CONFIG_FILE);
                        }
                    } else {
                        char hash[65], entry[128];
                        GenerateSaltedHash("password123", "salt2026", hash);
                        snprintf(entry, sizeof(entry), "salt2026,%s", hash);
                        WritePrivateProfileString("users", newName, entry, CONFIG_FILE);
                        WritePrivateProfileString("perms", newName, "0", CONFIG_FILE);
                    }
                    if (g_hSettingsWnd) {
                        HWND hList = GetProp(g_hSettingsWnd, "ListUsers");
                        if (hList) PopulateUserList(hList);
                    }
                }
            }
            SendHTTPResponse(ctx, "200 OK", "text/plain", "Saved", 5, NULL);
            return;
        }
        else if (strcmp(path, "/api/prefs") == 0) {
            if (body && contentLen > 0) {
                char* safeBody = malloc(contentLen + 1);
                memcpy(safeBody, body, contentLen);
                safeBody[contentLen] = '\0';
                char prefKey[256]; snprintf(prefKey, sizeof(prefKey), "%s_visible", authUser);
                WritePrivateProfileString("prefs", prefKey, safeBody, CONFIG_FILE);
                free(safeBody);
            } else {
                char prefKey[256]; snprintf(prefKey, sizeof(prefKey), "%s_visible", authUser);
                WritePrivateProfileString("prefs", prefKey, "", CONFIG_FILE);
            }
            SendHTTPResponse(ctx, "200 OK", "text/plain", "Saved", 5, NULL);
            return;
        }
        else if (strncmp(path, "/calendar/", 10) == 0) {
            if (strstr(path, ".ics") != NULL) {
                if (!body || contentLen <= 0) { SendHTTPResponse(ctx, "400 Bad Request", "text/plain", "No Body", 7, NULL); return; }
                
                char uid[128] = {0}, summary[256] = "New Event", dtstart[64] = {0}, dtend[64] = {0}, rrule[256] = {0};
                sscanf(path + 10, "%127[^.]", uid);
                
                char* p = strstr(body, "SUMMARY:");
                if (p) sscanf(p + 8, "%255[^\r\n]", summary);
                
                p = strstr(body, "DTSTART");
                if (p) { 
                    char *colon = strchr(p, ':');
                    char *newline = strchr(p, '\n');
                    if (colon && (!newline || colon < newline)) {
                        sscanf(colon + 1, "%63[^\r\n]", dtstart); 
                    }
                }
                
                p = strstr(body, "DTEND");
                if (p) { 
                    char *colon = strchr(p, ':');
                    char *newline = strchr(p, '\n');
                    if (colon && (!newline || colon < newline)) {
                        sscanf(colon + 1, "%63[^\r\n]", dtend); 
                    }
                }
                
                char* pRruleIn = strstr(body, "RRULE:");
                if (pRruleIn) sscanf(pRruleIn + 6, "%255[^\r\n]", rrule);
                
                int y=2026, m=10, d=26, h=12, min=0;
                if (strlen(dtstart) >= 15) {
                    sscanf(dtstart, "%4d%2d%2dT%2d%2d", &y, &m, &d, &h, &min);
                } else if (strlen(dtstart) >= 8) {
                    sscanf(dtstart, "%4d%2d%2d", &y, &m, &d);
                }
                int startMin = h * 60 + min;
                
                int ey=y, em=m, ed=d, eh=h+1, emin=min;
                if (strlen(dtend) >= 15) {
                    sscanf(dtend, "%4d%2d%2dT%2d%2d", &ey, &em, &ed, &eh, &emin);
                } else if (strlen(dtend) >= 8) {
                    sscanf(dtend, "%4d%2d%2d", &ey, &em, &ed);
                }
                
                int duration = (eh * 60 + emin) - startMin;
                if (duration <= 0) duration = 60;
                
                char dateStr[16]; snprintf(dateStr, sizeof(dateStr), "%04d/%02d/%02d", y, m, d);
                int quarter = ((m - 1) / 3) + 1;
                char filepath[MAX_PATH]; snprintf(filepath, sizeof(filepath), "%s/%04d-Q%d.json", DATA_DIR, y, quarter);
                
                int assignedIdx = GetUserPersonIdx(authUser);
                unsigned int etagHash = CalculateEventETag(summary, dateStr, startMin, duration, rrule);
                
                char newJson[1024];
                snprintf(newJson, sizeof(newJson), "{\"id\":\"%s\",\"title\":\"%s\",\"startMin\":%d,\"duration\":%d,\"color\":\"#039BE5\",\"date\":\"%s\",\"personIdx\":%d%s%s%s}", 
                    uid, summary, startMin, duration, dateStr, assignedIdx,
                    rrule[0] ? ",\"rrule\":\"" : "", rrule[0] ? rrule : "", rrule[0] ? "\"" : "");
                
                FILE* fp = fopen(filepath, "rb");
                char* fileBuf = NULL; long size = 0;
                if (fp) {
                    fseek(fp, 0, SEEK_END); size = ftell(fp); fseek(fp, 0, SEEK_SET);
                    fileBuf = malloc(size + 1); if (fileBuf) { fread(fileBuf, 1, size, fp); fileBuf[size] = '\0'; } fclose(fp);
                }
                
                char* outBuf = malloc((size > 0 ? size : 10) + 2048);
                strcpy(outBuf, "[");
                int first = 1;
                if (fileBuf) {
                    char* eventStart = fileBuf;
                    while ((eventStart = strstr(eventStart, "{\"id\":")) != NULL) {
                        char currentId[128] = {0};
                        char* pId = strstr(eventStart, "\"id\":\"");
                        if (pId) sscanf(pId + 6, "%63[^\"]", currentId);
                        
                        char* nextObj = strstr(eventStart + 1, "{\"id\":");
                        if (strcmp(currentId, uid) != 0) {
                            if (!first) strcat(outBuf, ",");
                            size_t copyLen = nextObj ? (size_t)(nextObj - eventStart) : strlen(eventStart);
                            while (copyLen > 0 && (eventStart[copyLen-1] == ']' || eventStart[copyLen-1] == ',' || eventStart[copyLen-1] == ' ' || eventStart[copyLen-1] == '\r' || eventStart[copyLen-1] == '\n')) {
                                copyLen--;
                            }
                            if (copyLen > 0) {
                                size_t currLen = strlen(outBuf);
                                memcpy(outBuf + currLen, eventStart, copyLen);
                                outBuf[currLen + copyLen] = '\0';
                            }
                            first = 0;
                        }
                        if (!nextObj) break;
                        eventStart = nextObj;
                    }
                    free(fileBuf);
                }
                
                if (!first) strcat(outBuf, ",");
                strcat(outBuf, newJson);
                strcat(outBuf, "]");
                
                fp = fopen(filepath, "wb");
                if (fp) { fwrite(outBuf, 1, strlen(outBuf), fp); fclose(fp); }
                free(outBuf);
                
                char header[256];
                snprintf(header, sizeof(header), "ETag: \"%s-%x\"\r\n", uid, etagHash);
                SendHTTPResponse(ctx, "201 Created", "text/plain", "", 0, header);
            }
            else {
                char filepath[MAX_PATH];
                snprintf(filepath, sizeof(filepath), "%s/%s", DATA_DIR, path + 10);
                if (body) {
                    FILE* fp = fopen(filepath, "wb");
                    if (fp) {
                        int bodyRead = strlen(body);
                        fwrite(body, 1, bodyRead, fp);
                        
                        int remaining = contentLen - bodyRead;
                        while (remaining > 0) {
                            char chunk[4096];
                            int r = TlsRecv(ctx, chunk, sizeof(chunk));
                            if (r <= 0) break;
                            fwrite(chunk, 1, r, fp);
                            remaining -= r;
                        }
                        
                        fclose(fp);
                        SendHTTPResponse(ctx, "201 Created", "text/plain", "Created", 7, NULL);
                    } else { SendHTTPResponse(ctx, "500 Internal Error", "text/plain", "I/O Error", 9, NULL); }
                } else { SendHTTPResponse(ctx, "400 Bad Request", "text/plain", "No Body", 7, NULL); }
            }
        }
    }
    else if (strcmp(method, "DELETE") == 0 && strncmp(path, "/calendar/", 10) == 0) {
        if (strstr(path, ".ics") != NULL) {
            char uid[128] = {0};
            sscanf(path + 10, "%127[^.]", uid);
            
            WIN32_FIND_DATA fd;
            char searchPath[MAX_PATH];
            snprintf(searchPath, sizeof(searchPath), "%s\\*.json", DATA_DIR);
            HANDLE hFind = FindFirstFile(searchPath, &fd);
            
            if (hFind != INVALID_HANDLE_VALUE) {
                do {
                    if (strcmp(fd.cFileName, "people.json") == 0 || strcmp(fd.cFileName, "contacts.json") == 0) continue;
                    
                    char filepath[MAX_PATH];
                    snprintf(filepath, sizeof(filepath), "%s/%s", DATA_DIR, fd.cFileName);
                    FILE* fp = fopen(filepath, "rb");
                    if (fp) {
                        fseek(fp, 0, SEEK_END); long size = ftell(fp); fseek(fp, 0, SEEK_SET);
                        char* fileBuf = malloc(size + 1);
                        if (fileBuf) {
                            fread(fileBuf, 1, size, fp); fileBuf[size] = '\0';
                            
                            char* outBuf = malloc(size + 1024);
                            strcpy(outBuf, "[");
                            int first = 1;
                            int modified = 0;
                            
                            char* eventStart = fileBuf;
                            while ((eventStart = strstr(eventStart, "{\"id\":")) != NULL) {
                                char currentId[128] = {0};
                                char* pId = strstr(eventStart, "\"id\":\"");
                                if (pId) sscanf(pId + 6, "%63[^\"]", currentId);
                                
                                char* nextObj = strstr(eventStart + 1, "{\"id\":");
                                if (strcmp(currentId, uid) != 0) {
                                    if (!first) strcat(outBuf, ",");
                                    size_t copyLen = nextObj ? (size_t)(nextObj - eventStart) : strlen(eventStart);
                                    while (copyLen > 0 && (eventStart[copyLen-1] == ']' || eventStart[copyLen-1] == ',' || eventStart[copyLen-1] == ' ' || eventStart[copyLen-1] == '\r' || eventStart[copyLen-1] == '\n')) {
                                        copyLen--;
                                    }
                                    if (copyLen > 0) {
                                        size_t currLen = strlen(outBuf);
                                        memcpy(outBuf + currLen, eventStart, copyLen);
                                        outBuf[currLen + copyLen] = '\0';
                                    }
                                    first = 0;
                                } else { modified = 1; }
                                
                                if (!nextObj) break;
                                eventStart = nextObj;
                            }
                            strcat(outBuf, "]");
                            fclose(fp);
                            
                            if (modified) {
                                fp = fopen(filepath, "wb");
                                if (fp) { fwrite(outBuf, 1, strlen(outBuf), fp); fclose(fp); }
                            }
                            free(outBuf);
                            free(fileBuf);
                        } else { fclose(fp); }
                    }
                } while (FindNextFile(hFind, &fd));
                FindClose(hFind);
            }
            SendHTTPResponse(ctx, "204 No Content", "text/plain", "", 0, NULL);
        } else {
            char filepath[MAX_PATH];
            snprintf(filepath, sizeof(filepath), "%s/%s", DATA_DIR, path + 10);
            remove(filepath);
            SendHTTPResponse(ctx, "204 No Content", "text/plain", "", 0, NULL);
        }
    }
}
LRESULT CALLBACK SettingsWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            CreateWindow("STATIC", "CalDAV Port:", WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE, 20, 20, 80, 20, hWnd, NULL, g_hInstance, NULL);
            HWND hEditPort = CreateWindow("EDIT", "", WS_CHILD | WS_VISIBLE | ES_NUMBER | WS_BORDER, 110, 17, 100, 25, hWnd, (HMENU)ID_EDIT_PORT, g_hInstance, NULL);
            CreateWindow("STATIC", "HTTP Port:", WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE, 20, 50, 80, 20, hWnd, NULL, g_hInstance, NULL);
            HWND hEditHttpPort = CreateWindow("EDIT", "", WS_CHILD | WS_VISIBLE | ES_NUMBER | WS_BORDER, 110, 47, 100, 25, hWnd, (HMENU)ID_EDIT_HTTP_PORT, g_hInstance, NULL);
            CreateWindow("STATIC", "Timeout (ms):", WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE, 20, 80, 80, 20, hWnd, NULL, g_hInstance, NULL);
            HWND hEditTimeout = CreateWindow("EDIT", "", WS_CHILD | WS_VISIBLE | ES_NUMBER | WS_BORDER, 110, 77, 100, 25, hWnd, (HMENU)ID_EDIT_TIMEOUT, g_hInstance, NULL);
            
            HWND hChkAnon = CreateWindow("BUTTON", "Anonymous Access", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 20, 112, 130, 20, hWnd, (HMENU)ID_CHK_ANON, g_hInstance, NULL);
            HWND hChkTls = CreateWindow("BUTTON", "Enable TLS/SSL", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 20, 132, 130, 20, hWnd, (HMENU)ID_CHK_TLS, g_hInstance, NULL);
            
            CreateWindow("BUTTON", "Start", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 220, 16, 80, 27, hWnd, (HMENU)ID_BTN_START, g_hInstance, NULL);
            CreateWindow("BUTTON", "Stop", WS_CHILD | WS_VISIBLE, 220, 46, 80, 27, hWnd, (HMENU)ID_BTN_STOP, g_hInstance, NULL);
            CreateWindow("BUTTON", "Trust Cert", WS_CHILD | WS_VISIBLE, 220, 76, 80, 27, hWnd, (HMENU)ID_BTN_INSTALL_CERT, g_hInstance, NULL);
            CreateWindow("BUTTON", "Export Cert", WS_CHILD | WS_VISIBLE, 220, 106, 80, 27, hWnd, (HMENU)ID_BTN_EXPORT_CERT, g_hInstance, NULL);
            CreateWindow("BUTTON", "Manage Cer", WS_CHILD | WS_VISIBLE, 220, 136, 80, 27, hWnd, (HMENU)ID_BTN_CERTMGR, g_hInstance, NULL);
            
            CreateWindow("STATIC", "Users (Select and press Del):", WS_CHILD | WS_VISIBLE | SS_LEFT, 20, 160, 200, 20, hWnd, NULL, g_hInstance, NULL);
            HWND hListUsers = CreateWindowEx(WS_EX_CLIENTEDGE, "LISTBOX", NULL, WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_VSCROLL | WS_BORDER, 20, 180, 120, 180, hWnd, (HMENU)ID_LIST_USERS, g_hInstance, NULL);
            
            OriginalListBoxProc = (WNDPROC)SetWindowLongPtr(hListUsers, GWLP_WNDPROC, (LONG_PTR)ListBoxSubclassProc);
            
            CreateWindow("STATIC", "Username:", WS_CHILD | WS_VISIBLE | SS_LEFT, 160, 180, 100, 20, hWnd, NULL, g_hInstance, NULL);
            HWND hEditUser = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 160, 200, 140, 25, hWnd, (HMENU)ID_EDIT_USER, g_hInstance, NULL);
            
            CreateWindow("STATIC", "Password:", WS_CHILD | WS_VISIBLE | SS_LEFT, 160, 235, 100, 20, hWnd, NULL, g_hInstance, NULL);
            HWND hEditPass = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_PASSWORD, 160, 255, 140, 25, hWnd, (HMENU)ID_EDIT_PASS, g_hInstance, NULL);
            
            HWND hChkEditAll = CreateWindow("BUTTON", "Can edit all events", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 160, 285, 140, 20, hWnd, (HMENU)ID_CHK_EDITALL, g_hInstance, NULL);
            
            CreateWindow("BUTTON", "Save User", WS_CHILD | WS_VISIBLE, 160, 320, 70, 30, hWnd, (HMENU)ID_BTN_ADDUSER, g_hInstance, NULL);
            CreateWindow("BUTTON", "Reset Pw", WS_CHILD | WS_VISIBLE, 235, 320, 65, 30, hWnd, (HMENU)23, g_hInstance, NULL); // ID_BTN_RESET_PASS
            
            char valStr[16];
            snprintf(valStr, sizeof(valStr), "%d", g_Port); SetWindowText(hEditPort, valStr);
            snprintf(valStr, sizeof(valStr), "%d", g_HttpPort); SetWindowText(hEditHttpPort, valStr);
            snprintf(valStr, sizeof(valStr), "%d", g_TimeoutMs); SetWindowText(hEditTimeout, valStr);
            
            SendMessage(hChkAnon, BM_SETCHECK, g_bAnonymous ? BST_CHECKED : BST_UNCHECKED, 0);
            SendMessage(hChkTls, BM_SETCHECK, g_bUseTls ? BST_CHECKED : BST_UNCHECKED, 0);
            
            EnableWindow(GetDlgItem(hWnd, ID_BTN_START), !g_bRunning);
            EnableWindow(GetDlgItem(hWnd, ID_BTN_STOP), g_bRunning);
            
            SetProp(hWnd, "EditPort", (HANDLE)hEditPort);
            SetProp(hWnd, "EditHttpPort", (HANDLE)hEditHttpPort);
            SetProp(hWnd, "EditTimeout", (HANDLE)hEditTimeout);
            SetProp(hWnd, "ListUsers", (HANDLE)hListUsers);
            SetProp(hWnd, "EditUser", (HANDLE)hEditUser);
            SetProp(hWnd, "EditPass", (HANDLE)hEditPass);
            SetProp(hWnd, "ChkEditAll", (HANDLE)hChkEditAll);
            
            PopulateUserList(hListUsers);
            return 0;
        }
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT: {
            HDC hdcStatic = (HDC)wParam; SetTextColor(hdcStatic, RGB(0, 0, 0)); SetBkColor(hdcStatic, RGB(255, 255, 255));
            return (LRESULT)GetStockObject(WHITE_BRUSH);
        }
        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                case ID_EDIT_PORT:
                case ID_EDIT_HTTP_PORT:
                case ID_EDIT_TIMEOUT:
                    if (HIWORD(wParam) == EN_CHANGE) { KillTimer(hWnd, 1); SetTimer(hWnd, 1, PORT_UPDATE_DELAY_MS, UpdateTimerCallback); }
                    return 0;
                case ID_CHK_ANON: g_bAnonymous = (SendMessage((HWND)lParam, BM_GETCHECK, 0, 0) == BST_CHECKED); SaveConfig(); return 0;
                case ID_CHK_TLS: g_bUseTls = (SendMessage((HWND)lParam, BM_GETCHECK, 0, 0) == BST_CHECKED); SaveConfig(); return 0;
                case ID_BTN_CERTMGR:
                    ShellExecute(NULL, "open", "certmgr.msc", NULL, NULL, SW_SHOWNORMAL);
                    return 0;
                case 23: { // ID_BTN_RESET_PASS
                    HWND hList = GetProp(hWnd, "ListUsers");
                    int idx = SendMessage(hList, LB_GETCURSEL, 0, 0);
                    if (idx != LB_ERR) {
                        char user[256]; SendMessage(hList, LB_GETTEXT, idx, (LPARAM)user);
                        char newPass[16];
                        snprintf(newPass, sizeof(newPass), "%04x%04x", rand() % 0xFFFF, rand() % 0xFFFF);
                        
                        char hash[65], entry[128];
                        GenerateSaltedHash(newPass, "salt2026", hash);
                        snprintf(entry, sizeof(entry), "salt2026,%s", hash);
                        WritePrivateProfileString("users", user, entry, CONFIG_FILE);
                        
                        if (OpenClipboard(hWnd)) {
                            EmptyClipboard();
                            HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, strlen(newPass) + 1);
                            if (hMem) {
                                memcpy(GlobalLock(hMem), newPass, strlen(newPass) + 1);
                                GlobalUnlock(hMem);
                                SetClipboardData(CF_TEXT, hMem);
                            }
                            CloseClipboard();
                            MessageBox(hWnd, "New password copied to clipboard!", SERVER_NAME, MB_ICONINFORMATION);
                        }
                    } else {
                        MessageBox(hWnd, "Please select a user from the list first.", SERVER_NAME, MB_ICONERROR);
                    }
                    return 0;
                }
                case ID_BTN_INSTALL_CERT: {
                    PCCERT_CONTEXT pCert = GenerateCertificate();
                    if (pCert) {
                        MessageBox(hWnd, "Root CA certificate injected into Trusted Root Authorities.\nBrowsers will now trust this server.", SERVER_NAME, MB_ICONINFORMATION);
                        CertFreeCertificateContext(pCert);
                    } else {
                        MessageBox(hWnd, "Failed to generate or trust certificates.", SERVER_NAME, MB_ICONERROR);
                    }
                    return 0;
                }
                case ID_BTN_EXPORT_CERT: {
                    PCCERT_CONTEXT pCert = GenerateCertificate();
                    if (pCert) {
                        OPENFILENAMEA ofn = {0};
                        char szFile[MAX_PATH] = "CalDAV_CertBundle.pfx";
                        ofn.lStructSize = sizeof(ofn);
                        ofn.hwndOwner = hWnd;
                        ofn.lpstrFilter = "PFX Archive (*.pfx)\0*.pfx\0All Files (*.*)\0*.*\0";
                        ofn.lpstrFile = szFile;
                        ofn.nMaxFile = MAX_PATH;
                        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
                        ofn.lpstrDefExt = "pfx";
                        
                        if (GetSaveFileNameA(&ofn)) {
                            HCERTSTORE hMemStore = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, CERT_STORE_CREATE_NEW_FLAG, NULL);
                            if (hMemStore) {
                                CertAddCertificateContextToStore(hMemStore, pCert, CERT_STORE_ADD_NEW, NULL);
                                HCERTSTORE hSysStore = CertOpenStore(CERT_STORE_PROV_SYSTEM, 0, 0, CERT_SYSTEM_STORE_CURRENT_USER, L"Root");
                                if (hSysStore) {
                                    PCCERT_CONTEXT pRoot = CertFindCertificateInStore(hSysStore, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0, CERT_FIND_SUBJECT_STR_A, "CalDAV Local Root CA", NULL);
                                    if (pRoot) {
                                        CertAddCertificateContextToStore(hMemStore, pRoot, CERT_STORE_ADD_NEW, NULL);
                                        CertFreeCertificateContext(pRoot);
                                    }
                                    CertCloseStore(hSysStore, 0);
                                }

                                CRYPT_DATA_BLOB pfxBlob = {0};
                                LPCWSTR pfxPassword = L"caldav";
                                
                                if (PFXExportCertStore(hMemStore, &pfxBlob, pfxPassword, EXPORT_PRIVATE_KEYS)) {
                                    pfxBlob.pbData = (BYTE*)malloc(pfxBlob.cbData);
                                    if (PFXExportCertStore(hMemStore, &pfxBlob, pfxPassword, EXPORT_PRIVATE_KEYS)) {
                                        FILE* f = fopen(szFile, "wb");
                                        if (f) {
                                            fwrite(pfxBlob.pbData, 1, pfxBlob.cbData, f);
                                            fclose(f);
                                            MessageBox(hWnd, "PFX bundle exported successfully.\nImport password is: caldav", SERVER_NAME, MB_ICONINFORMATION);
                                        } else {
                                            MessageBox(hWnd, "Failed to write PFX file.", SERVER_NAME, MB_ICONERROR);
                                        }
                                    }
                                    free(pfxBlob.pbData);
                                } else {
                                    MessageBox(hWnd, "Failed to encode PFX blob.", SERVER_NAME, MB_ICONERROR);
                                }
                                CertCloseStore(hMemStore, 0);
                            }
                        }
                        CertFreeCertificateContext(pCert);
                    } else {
                        MessageBox(hWnd, "Failed to generate certificate chain.", SERVER_NAME, MB_ICONERROR);
                    }
                    return 0;
                }
               case ID_LIST_USERS:
                    if (HIWORD(wParam) == LBN_SELCHANGE) {
                        HWND hList = GetProp(hWnd, "ListUsers");
                        HWND hUser = GetProp(hWnd, "EditUser");
                        int idx = SendMessage(hList, LB_GETCURSEL, 0, 0);
                        if (idx != LB_ERR) {
                            char user[256]; SendMessage(hList, LB_GETTEXT, idx, (LPARAM)user);
                            SetWindowText(hUser, user); SetWindowText(GetProp(hWnd, "EditPass"), "");
                            
                            char canEdit[8] = "0";
                            GetPrivateProfileString("perms", user, "0", canEdit, sizeof(canEdit), CONFIG_FILE);
                            SendMessage(GetProp(hWnd, "ChkEditAll"), BM_SETCHECK, canEdit[0]=='1' ? BST_CHECKED : BST_UNCHECKED, 0);
                        }
                    }
                    return 0;
                case ID_BTN_ADDUSER: {
                    HWND hUser = GetProp(hWnd, "EditUser");
                    HWND hPass = GetProp(hWnd, "EditPass");
                    HWND hList = GetProp(hWnd, "ListUsers");
                    char user[256], pass[256];
                    GetWindowText(hUser, user, sizeof(user)); 
                    GetWindowText(hPass, pass, sizeof(pass));
                    
                    if (user[0] && pass[0]) {
                        char hash[65], entry[128];
                        GenerateSaltedHash(pass, "salt2026", hash);
                        snprintf(entry, sizeof(entry), "salt2026,%s", hash);
                        WritePrivateProfileString("users", user, entry, CONFIG_FILE); 
                        
                        HWND hChkEditAll = GetProp(hWnd, "ChkEditAll");
                        BOOL canEdit = SendMessage(hChkEditAll, BM_GETCHECK, 0, 0) == BST_CHECKED;
                        WritePrivateProfileString("perms", user, canEdit ? "1" : "0", CONFIG_FILE);
                        
                        PopulateUserList(hList); 
                        SetWindowText(hUser, ""); 
                        SetWindowText(hPass, "");
                    }
                    return 0;
                }

                 case ID_BTN_START:
                    if (!g_bRunning) {
                        HWND hEdit = GetProp(hWnd, "EditPort"), hEditHttp = GetProp(hWnd, "EditHttpPort");
                        if (hEdit && hEditHttp) {
                            char portStr[16];
                            GetWindowText((HWND)hEdit, portStr, sizeof(portStr)); g_Port = atoi(portStr);
                            GetWindowText((HWND)hEditHttp, portStr, sizeof(portStr)); g_HttpPort = atoi(portStr);
                            
                            if (InitTLS() && StartServer(g_Port)) {
                                g_bRunning = TRUE;
                                HANDLE hThread = (HANDLE)_beginthreadex(NULL, 0, ServerThread, NULL, 0, NULL);
                                if (hThread) CloseHandle(hThread);
                                if (StartHttpServer(g_HttpPort)) {
                                    HANDLE hHttpThread = (HANDLE)_beginthreadex(NULL, 0, HttpServerThread, NULL, 0, NULL);
                                    if (hHttpThread) CloseHandle(hHttpThread);
                                }
                                EnableWindow(GetDlgItem(hWnd, ID_BTN_START), FALSE); EnableWindow(GetDlgItem(hWnd, ID_BTN_STOP), TRUE);
                                snprintf(g_nid.szTip, sizeof(g_nid.szTip), "%s - Running (TLS:%s)", SERVER_NAME, g_bUseTls ? "On" : "Off");
                                Shell_NotifyIcon(NIM_MODIFY, &g_nid);
                            } else { MessageBox(hWnd, "Failed to start server or TLS.", SERVER_NAME, MB_ICONERROR); CleanupTLS(); }
                        }
                    }
                    return 0;
                case ID_BTN_STOP:
                    if (g_bRunning) {
                        StopServer(); CleanupTLS(); g_bRunning = FALSE;
                        EnableWindow(GetDlgItem(hWnd, ID_BTN_START), TRUE); EnableWindow(GetDlgItem(hWnd, ID_BTN_STOP), FALSE);
                        snprintf(g_nid.szTip, sizeof(g_nid.szTip), "%s - Stopped", SERVER_NAME); Shell_NotifyIcon(NIM_MODIFY, &g_nid);
                    }
                    return 0;
            }
            return DefWindowProc(hWnd, msg, wParam, lParam);
        }
        case WM_CLOSE: ShowWindow(hWnd, SW_HIDE); return 0;
        case WM_DESTROY:
            RemoveProp(hWnd, "EditPort"); RemoveProp(hWnd, "EditHttpPort"); RemoveProp(hWnd, "EditTimeout");
            RemoveProp(hWnd, "ListUsers"); RemoveProp(hWnd, "EditUser"); RemoveProp(hWnd, "EditPass"); RemoveProp(hWnd, "ChkEditAll");
            PostQuitMessage(0); return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}
VOID CALLBACK UpdateTimerCallback(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime) {
    (void)uMsg; (void)idEvent; (void)dwTime;
    KillTimer(hwnd, idEvent);
    if (g_hSettingsWnd) {
        HWND hEditPort = GetProp(g_hSettingsWnd, "EditPort");
        if (hEditPort) {
            char portStr[16]; GetWindowText(hEditPort, portStr, sizeof(portStr));
            int newPort = atoi(portStr); if (newPort >= 1 && newPort <= 65535 && newPort != g_Port) g_Port = newPort;
        }
        HWND hEditHttpPort = GetProp(g_hSettingsWnd, "EditHttpPort");
        if (hEditHttpPort) {
            char portStr[16]; GetWindowText(hEditHttpPort, portStr, sizeof(portStr));
            int newHttpPort = atoi(portStr); if (newHttpPort >= 1 && newHttpPort <= 65535 && newHttpPort != g_HttpPort) g_HttpPort = newHttpPort;
        }
        HWND hEditTimeout = GetProp(g_hSettingsWnd, "EditTimeout");
        if (hEditTimeout) {
            char timeoutStr[16]; GetWindowText(hEditTimeout, timeoutStr, sizeof(timeoutStr));
            int newTimeout = atoi(timeoutStr); if (newTimeout > 0 && newTimeout != g_TimeoutMs) g_TimeoutMs = newTimeout;
        }
        SaveConfig();
    }
}

INT_PTR CreateSettingsWindow(void) {
    WNDCLASSEX wc = {0};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = SettingsWndProc;
    wc.hInstance = g_hInstance;
    wc.lpszClassName = "CaldAVDSettingsClass";
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassEx(&wc);
    g_hSettingsWnd = CreateWindowEx(0, "CaldAVDSettingsClass", "CalDAV TLS Settings", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 350, 420, NULL, NULL, g_hInstance, NULL);
    if (g_hSettingsWnd) ShowWindow(g_hSettingsWnd, SW_SHOWNORMAL);
    return (INT_PTR)g_hSettingsWnd;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: g_TaskbarCreatedMsg = RegisterWindowMessage("TaskbarCreated"); break;
        case WM_TRAYICON: if (lParam == WM_RBUTTONUP || lParam == WM_LBUTTONUP) ShowContextMenu(hWnd); break;
        case WM_COMMAND:
            switch (wParam) {
                case ID_MENU_START:
                    if (!g_bRunning && LoadConfig() && InitTLS() && StartServer(g_Port)) {
                        g_bRunning = TRUE;
                        HANDLE hThread = (HANDLE)_beginthreadex(NULL, 0, ServerThread, NULL, 0, NULL);
                        if (hThread) CloseHandle(hThread);
                        if (StartHttpServer(g_HttpPort)) {
                            HANDLE hHttpThread = (HANDLE)_beginthreadex(NULL, 0, HttpServerThread, NULL, 0, NULL);
                            if (hHttpThread) CloseHandle(hHttpThread);
                        }
                        snprintf(g_nid.szTip, sizeof(g_nid.szTip), "%s - Running", SERVER_NAME); Shell_NotifyIcon(NIM_MODIFY, &g_nid);
                    }
                    break;
                case ID_MENU_STOP: if (g_bRunning) { StopServer(); CleanupTLS(); g_bRunning = FALSE; snprintf(g_nid.szTip, sizeof(g_nid.szTip), "%s - Stopped", SERVER_NAME); Shell_NotifyIcon(NIM_MODIFY, &g_nid); } break;
                case ID_MENU_SETTINGS: CreateSettingsWindow(); break;
                
                case ID_MENU_IMPORT: {
                    OPENFILENAMEA ofn = {0};
                    char szFile[MAX_PATH] = {0};
                    ofn.lStructSize = sizeof(ofn);
                    ofn.hwndOwner = hWnd;
                    ofn.lpstrFilter = "iCalendar Files (*.ics)\0*.ics\0All Files (*.*)\0*.*\0";
                    ofn.lpstrFile = szFile;
                    ofn.nMaxFile = MAX_PATH;
                    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
                    if (GetOpenFileNameA(&ofn)) {
                        FILE* fIn = fopen(szFile, "rb");
                        if (fIn) {
                            fseek(fIn, 0, SEEK_END);
                            long size = ftell(fIn);
                            fseek(fIn, 0, SEEK_SET);
                            char* icsBuf = malloc(size + 1);
                            if (icsBuf) {
                                fread(icsBuf, 1, size, fIn);
                                icsBuf[size] = '\0';
                                
                                char* eventStart = icsBuf;
                                int importedCount = 0;
                                while ((eventStart = strstr(eventStart, "BEGIN:VEVENT")) != NULL) {
                                    char uid[128] = {0}, summary[256] = "Imported Event", dtstart[32] = {0}, dtend[32] = {0};
                                    
                                    char* p = strstr(eventStart, "UID:");
                                    if (p) sscanf(p + 4, "%127[^\r\n]", uid);
                                    else snprintf(uid, sizeof(uid), "imp_%u_%d", GetTickCount(), importedCount);
                                    
                                    p = strstr(eventStart, "SUMMARY:");
                                    if (p) sscanf(p + 8, "%255[^\r\n]", summary);
                                    
                                    p = strstr(eventStart, "DTSTART");
                                    if (p) { p = strchr(p, ':'); if (p) sscanf(p + 1, "%31[^\r\n]", dtstart); }
                                    
                                    p = strstr(eventStart, "DTEND");
                                    if (p) { p = strchr(p, ':'); if (p) sscanf(p + 1, "%31[^\r\n]", dtend); }
                                    
                                    int y=2026, m=1, d=1, h=12, min=0;
                                    if (strlen(dtstart) >= 8) sscanf(dtstart, "%4d%2d%2dT%2d%2d", &y, &m, &d, &h, &min);
                                    int startMins = h * 60 + min;
                                    
                                    int ey=y, em=m, ed=d, eh=h+1, emin=min;
                                    if (strlen(dtend) >= 8) sscanf(dtend, "%4d%2d%2dT%2d%2d", &ey, &em, &ed, &eh, &emin);
                                    int duration = (eh * 60 + emin) - startMins;
                                    if (duration <= 0) duration = 60;
                                    
                                    int quarter = ((m - 1) / 3) + 1;
                                    char qFile[MAX_PATH]; snprintf(qFile, sizeof(qFile), "%s/%04d-Q%d.json", DATA_DIR, y, quarter);
                                    
                                    char newJson[1024];
                                    snprintf(newJson, sizeof(newJson), "{\"id\":\"%s\",\"title\":\"%s\",\"startMin\":%d,\"duration\":%d,\"color\":\"#33B679\",\"date\":\"%04d/%02d/%02d\",\"personIdx\":0}", uid, summary, startMins, duration, y, m, d);
                                    
                                    FILE* fp = fopen(qFile, "rb");
                                    char* fileBuf = NULL; long fsize = 0;
                                    if (fp) {
                                        fseek(fp, 0, SEEK_END); fsize = ftell(fp); fseek(fp, 0, SEEK_SET);
                                        fileBuf = malloc(fsize + 1); if (fileBuf) { fread(fileBuf, 1, fsize, fp); fileBuf[fsize] = '\0'; } fclose(fp);
                                    }
                                    
                                    char* outBuf = malloc((fsize > 0 ? fsize : 10) + 2048);
                                    strcpy(outBuf, "[");
                                    int first = 1;
                                    if (fileBuf) {
                                        char* eStart = fileBuf;
                                        while ((eStart = strstr(eStart, "{\"id\":")) != NULL) {
                                            char currentId[128] = {0};
                                            char* pId = strstr(eStart, "\"id\":\"");
                                            if (pId) sscanf(pId + 6, "%63[^\"]", currentId);
                                            
                                            char* nextObj = strstr(eStart + 1, "{\"id\":");
                                            if (strcmp(currentId, uid) != 0) {
                                                if (!first) strcat(outBuf, ",");
                                                size_t copyLen = nextObj ? (size_t)(nextObj - eStart) : strlen(eStart);
                                                while (copyLen > 0 && (eStart[copyLen-1] == ']' || eStart[copyLen-1] == ',' || eStart[copyLen-1] == ' ' || eStart[copyLen-1] == '\r' || eStart[copyLen-1] == '\n')) {
                                                    copyLen--;
                                                }
                                                if (copyLen > 0) {
                                                    size_t currLen = strlen(outBuf);
                                                    memcpy(outBuf + currLen, eStart, copyLen);
                                                    outBuf[currLen + copyLen] = '\0';
                                                }
                                                first = 0;
                                            }
                                            if (!nextObj) break;
                                            eStart = nextObj;
                                        }
                                        free(fileBuf);
                                    }
                                    if (!first) strcat(outBuf, ",");
                                    strcat(outBuf, newJson);
                                    strcat(outBuf, "]");
                                    
                                    fp = fopen(qFile, "wb");
                                    if (fp) { fwrite(outBuf, 1, strlen(outBuf), fp); fclose(fp); }
                                    free(outBuf);
                                    
                                    importedCount++;
                                    eventStart++;
                                }
                                free(icsBuf);
                                char msg[256]; snprintf(msg, sizeof(msg), "Successfully imported %d events from ICS.", importedCount);
                                MessageBoxA(hWnd, msg, SERVER_NAME, MB_ICONINFORMATION);
                            }
                            fclose(fIn);
                        }
                    }
                    break;
                }
                case ID_MENU_EXPORT: {
                    OPENFILENAMEA ofn = {0};
                    char szFile[MAX_PATH] = "calendar_export.ics";
                    ofn.lStructSize = sizeof(ofn);
                    ofn.hwndOwner = hWnd;
                    ofn.lpstrFilter = "iCalendar Files (*.ics)\0*.ics\0";
                    ofn.lpstrFile = szFile;
                    ofn.nMaxFile = MAX_PATH;
                    ofn.Flags = OFN_OVERWRITEPROMPT;
                    ofn.lpstrDefExt = "ics";
                    if (GetSaveFileNameA(&ofn)) {
                        FILE* fOut = fopen(szFile, "wb");
                        if (fOut) {
                            const char* header = "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//CalDAV-D//EN\r\n";
                            fwrite(header, 1, strlen(header), fOut);
                            
                            WIN32_FIND_DATA fd;
                            char searchPath[MAX_PATH];
                            snprintf(searchPath, sizeof(searchPath), "%s\\*.json", DATA_DIR);
                            HANDLE hFind = FindFirstFile(searchPath, &fd);
                            
                            if (hFind != INVALID_HANDLE_VALUE) {
                                do {
                                    if (strcmp(fd.cFileName, "people.json") == 0) continue;
                                    char filepath[MAX_PATH];
                                    snprintf(filepath, sizeof(filepath), "%s/%s", DATA_DIR, fd.cFileName);
                                    FILE* fp = fopen(filepath, "rb");
                                    if (fp) {
                                        fseek(fp, 0, SEEK_END); long size = ftell(fp); fseek(fp, 0, SEEK_SET);
                                        char* fileBuf = malloc(size + 1);
                                        if (fileBuf) {
                                            fread(fileBuf, 1, size, fp); fileBuf[size] = '\0';
                                            
                                            char* eventStart = fileBuf;
                                            while ((eventStart = strstr(eventStart, "{\"id\":")) != NULL) {
                                                char id[64] = {0}, title[128] = "Untitled Event", date[16] = {0};
                                                int startMin = 0, duration = 60;
                                                
                                                char* pId = strstr(eventStart, "\"id\":\"");
                                                if (pId) sscanf(pId + 6, "%63[^\"]", id);
                                                
                                                char* pTitle = strstr(eventStart, "\"title\":\"");
                                                if (pTitle) sscanf(pTitle + 9, "%127[^\"]", title);
                                                
                                                char* pDate = strstr(eventStart, "\"date\":\"");
                                                if (pDate) sscanf(pDate + 8, "%15[^\"]", date);
                                                
                                                char* pStart = strstr(eventStart, "\"startMin\":");
                                                if (pStart) sscanf(pStart + 11, "%d", &startMin);
                                                
                                                char* pDur = strstr(eventStart, "\"duration\":");
                                                if (pDur) sscanf(pDur + 11, "%d", &duration);
                                                
                                                char icsDate[9] = {0};
                                                if (strlen(date) >= 10) {
                                                    icsDate[0] = date[0]; icsDate[1] = date[1]; icsDate[2] = date[2]; icsDate[3] = date[3];
                                                    icsDate[4] = date[5]; icsDate[5] = date[6];
                                                    icsDate[6] = date[8]; icsDate[7] = date[9];
                                                }
                                                
                                                int startH = startMin / 60;
                                                int startM = startMin % 60;
                                                int endMin = startMin + duration;
                                                int endH = endMin / 60;
                                                int endM = endMin % 60;
                                                
                                                char eventBuf[2048];
                                                snprintf(eventBuf, sizeof(eventBuf),
                                                    "BEGIN:VEVENT\r\n"
                                                    "UID:%s\r\n"
                                                    "DTSTAMP:%sT%02d%02d00\r\n"
                                                    "DTSTART:%sT%02d%02d00\r\n"
                                                    "DTEND:%sT%02d%02d00\r\n"
                                                    "SUMMARY:%s\r\n"
                                                    "END:VEVENT\r\n",
                                                    id, icsDate, startH, startM, icsDate, startH, startM, icsDate, endH, endM, title);
                                                    
                                                fwrite(eventBuf, 1, strlen(eventBuf), fOut);
                                                eventStart++;
                                            }
                                            free(fileBuf);
                                        }
                                        fclose(fp);
                                    }
                                } while (FindNextFile(hFind, &fd));
                                FindClose(hFind);
                            }
                            const char* footer = "END:VCALENDAR\r\n";
                            fwrite(footer, 1, strlen(footer), fOut);
                            fclose(fOut);
                            MessageBoxA(hWnd, "Calendar exported to ICS successfully.", SERVER_NAME, MB_ICONINFORMATION);
                        }
                    }
                    break;
                }
                case ID_MENU_EXIT: DestroyWindow(hWnd); break;
            }
            break;
        case WM_CLOSE: if (g_bRunning) { StopServer(); CleanupTLS(); } DestroyWindow(hWnd); break;
        case WM_DESTROY: RemoveTrayIcon(); PostQuitMessage(0); break;
        default: if (msg == g_TaskbarCreatedMsg && g_TaskbarCreatedMsg != 0) { CreateTrayIcon(hWnd); return 0; } break;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}
VOID CreateTrayIcon(HWND hWnd) {
    g_nid.hWnd = hWnd; g_nid.uID = ID_TRAY_ICON; g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON; g_nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    snprintf(g_nid.szTip, sizeof(g_nid.szTip), "%s - %s", SERVER_NAME, g_bRunning ? "Running" : "Stopped"); Shell_NotifyIcon(NIM_ADD, &g_nid);
}
VOID RemoveTrayIcon() { Shell_NotifyIcon(NIM_DELETE, &g_nid); }

VOID ShowContextMenu(HWND hWnd) {
    POINT pt; HMENU hMenu = CreatePopupMenu(); GetCursorPos(&pt);
    AppendMenu(hMenu, MF_STRING | (g_bRunning ? MF_CHECKED : MF_UNCHECKED), ID_MENU_START, "Start Server\tCtrl+S");
    AppendMenu(hMenu, MF_STRING | (!g_bRunning ? MF_CHECKED : MF_UNCHECKED), ID_MENU_STOP, "Stop Server\tCtrl+T");
    AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(hMenu, MF_STRING, ID_MENU_IMPORT, "Import Calendar (.ics)");
    AppendMenu(hMenu, MF_STRING, ID_MENU_EXPORT, "Export Calendar (.ics)");
    AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(hMenu, MF_STRING, ID_MENU_SETTINGS, "Settings...\tCtrl+P");
    AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(hMenu, MF_STRING, ID_MENU_EXIT, "Exit\tAlt+F4");
    SetForegroundWindow(hWnd); TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hWnd, NULL); DestroyMenu(hMenu);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrevInst, LPSTR cmdLine, int cmdShow) {
    (void)hPrevInst; (void)cmdLine; (void)cmdShow; g_hInstance = hInst;
    INITCOMMONCONTROLSEX icc = { sizeof(INITCOMMONCONTROLSEX), ICC_WIN95_CLASSES }; InitCommonControlsEx(&icc);
    LoadConfig(); SeedDefaultUser(); CreateDirectory(DATA_DIR, NULL); CreateDirectory(WEB_DIR, NULL);
    
    WNDCLASSEX wc = {0}; wc.cbSize = sizeof(WNDCLASSEX); wc.lpfnWndProc = WndProc; wc.hInstance = g_hInstance;
    wc.lpszClassName = "CaldAVDMainClass"; wc.hIcon = LoadIcon(NULL, IDI_APPLICATION); RegisterClassEx(&wc);
    
    g_hMainWnd = CreateWindowEx(0, "CaldAVDMainClass", SERVER_NAME, WS_OVERLAPPEDWINDOW, 0, 0, 0, 0, NULL, NULL, g_hInstance, NULL);
    if (!g_hMainWnd) return 1;
    
    if (InitTLS() && StartServer(g_Port)) {
        g_bRunning = TRUE;
        HANDLE hThread = (HANDLE)_beginthreadex(NULL, 0, ServerThread, NULL, 0, NULL); if (hThread) CloseHandle(hThread);
        if (StartHttpServer(g_HttpPort)) {
            HANDLE hHttpThread = (HANDLE)_beginthreadex(NULL, 0, HttpServerThread, NULL, 0, NULL); if (hHttpThread) CloseHandle(hHttpThread);
        }
    }

    CreateTrayIcon(g_hMainWnd);
    MSG msg; while (GetMessage(&msg, NULL, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessage(&msg); }
    if (g_bRunning) { StopServer(); CleanupTLS(); }
    return (int)msg.wParam;
}
