#ifndef _RECEIVING_H
#define _RECEIVING_H

#include "account-data.h"
#include "transceiver.h"
#include <purple.h>

std::string makeNoticeWithSender(const td::td_api::chat &chat, const TgMessageInfo &message,
                                 const char *noticeText, PurpleAccount *account);
std::string getMessageText(const td::td_api::formattedText &text);
std::string makeInlineImageText(int imgstoreId);
void showMessageText(TdAccountData &account, const td::td_api::chat &chat, const TgMessageInfo &message,
                     const char *text, const char *notification, uint32_t extraFlags = 0);
void showMessageTextIm(TdAccountData &account, const char *purpleUserName, const char *text,
                       const char *notification, time_t timestamp, PurpleMessageFlags flags);
void showChatNotification(TdAccountData &account, const td::td_api::chat &chat,
                          const char *notification, PurpleMessageFlags extraFlags = (PurpleMessageFlags)0);
void showGenericFileInline(const td::td_api::chat &chat, const TgMessageInfo &message,
                           const std::string &filePath, const std::string &fileDescription,
                           TdAccountData &account);

const td::td_api::file *selectPhotoSize(PurpleAccount *account, const td::td_api::messagePhoto &photo);
void makeFullMessage(td::td_api::object_ptr<td::td_api::message> message, IncomingMessage &fullMessage,
                     const TdAccountData &account);
bool isMessageReady(const IncomingMessage &fullMessage, const TdAccountData &account);
void fetchExtras(const IncomingMessage &fullMessage, TdTransceiver &transceiver, TdAccountData &account,
                 TdTransceiver::ResponseCb2 onFetchReply);

#endif

