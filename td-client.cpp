#include "td-client.h"
#include "config.h"

class UpdateHandler {
public:
    UpdateHandler(PurpleTdClient *owner) : m_owner(owner) {}

    void operator()(td::td_api::updateAuthorizationState &update_authorization_state) const {
        purple_debug_misc(config::pluginId, "Incoming update: authorization state\n");
        m_owner->m_lastAuthState = update_authorization_state.authorization_state_->get_id();
        td::td_api::downcast_call(*update_authorization_state.authorization_state_, *m_owner->m_authUpdateHandler);
    }

    void operator()(td::td_api::updateConnectionState &connectionUpdate) const {
        purple_debug_misc(config::pluginId, "Incoming update: connection state\n");
        if (connectionUpdate.state_ && (connectionUpdate.state_->get_id() == td::td_api::connectionStateReady::ID))
            m_owner->connectionReady();
    }

    void operator()(td::td_api::updateUser &userUpdate) const {
        purple_debug_misc(config::pluginId, "Incoming update: update user\n");
        TdAccountData::Lock lock(m_owner->m_data);
        m_owner->m_data.updateUser(std::move(userUpdate.user_));
    }

    void operator()(td::td_api::updateNewChat &newChat) const {
        purple_debug_misc(config::pluginId, "Incoming update: new chat\n");
        TdAccountData::Lock lock(m_owner->m_data);
        m_owner->m_data.addNewChat(std::move(newChat.chat_));
    }

    void operator()(td::td_api::updateNewMessage &newMessageUpdate) const {
        purple_debug_misc(config::pluginId, "Incoming update: new message\n");
        if (newMessageUpdate.message_)
            m_owner->onIncomingMessage(std::move(newMessageUpdate.message_));
        else
            purple_debug_warning(config::pluginId, "Received null new message\n");
    }

    void operator()(auto &update) const {
        purple_debug_misc(config::pluginId, "Incoming update: ignorig ID=%d\n", update.get_id());
    }
private:
    PurpleTdClient *m_owner;
};

class AuthUpdateHandler {
public:
    AuthUpdateHandler(PurpleTdClient *owner) : m_owner(owner) {}

    void operator()(td::td_api::authorizationStateWaitEncryptionKey &) const {
        purple_debug_misc(config::pluginId, "Authorization state update: encriytion key requested\n");
        m_owner->sendQuery(td::td_api::make_object<td::td_api::checkDatabaseEncryptionKey>(""), &PurpleTdClient::authResponse);
    }

    void operator()(td::td_api::authorizationStateWaitTdlibParameters &) const {
        purple_debug_misc(config::pluginId, "Authorization state update: TDLib parameters requested\n");
        m_owner->sendTdlibParameters();
    }

    void operator()(td::td_api::authorizationStateWaitPhoneNumber &) const {
        purple_debug_misc(config::pluginId, "Authorization state update: phone number requested\n");
        m_owner->sendPhoneNumber();
    }

    void operator()(td::td_api::authorizationStateWaitCode &codeState) const {
        purple_debug_misc(config::pluginId, "Authorization state update: authentication code requested\n");
        m_owner->m_authCodeInfo = std::move(codeState.code_info_);
        g_idle_add(PurpleTdClient::requestAuthCode, m_owner);
    }

    void operator()(td::td_api::authorizationStateReady &) const {
        purple_debug_misc(config::pluginId, "Authorization state update: ready\n");
    }

    void operator()(auto &update) const {
        purple_debug_misc(config::pluginId, "Authorization state update: ignorig ID=%d\n", update.get_id());
    }
private:
    PurpleTdClient *m_owner;
};

PurpleTdClient::PurpleTdClient(PurpleAccount *acct)
{
    m_account           = acct;
    m_updateHandler     = std::make_unique<UpdateHandler>(this);
    m_authUpdateHandler = std::make_unique<AuthUpdateHandler>(this);
}

PurpleTdClient::~PurpleTdClient()
{
    m_stopThread = true;
    m_client->send({UINT64_MAX, td::td_api::make_object<td::td_api::close>()});
    m_pollThread.join();
}

void PurpleTdClient::setLogLevel(int level)
{
    // Why not just call setLogVerbosityLevel? No idea!
    td::Client::execute({0, td::td_api::make_object<td::td_api::setLogVerbosityLevel>(level)});
}

void PurpleTdClient::startLogin()
{
#if !GLIB_CHECK_VERSION(2, 32, 0)
    // GLib threading system is automaticaly initialized since 2.32.
    // For earlier versions, it have to be initialized before calling any
    // Glib or GTK+ functions.
    if (!g_thread_supported())
        g_thread_init(NULL);
#endif

    m_client = std::make_unique<td::Client>();
    if (!m_pollThread.joinable()) {
        m_lastQueryId = 0;
        m_stopThread = false;
        m_pollThread = std::thread([this]() { pollThreadLoop(); });
    }
}

void PurpleTdClient::pollThreadLoop()
{
    while (!m_stopThread)
        processResponse(m_client->receive(1));
}

void PurpleTdClient::processResponse(td::Client::Response response)
{
    if (response.object) {
        if (response.id == 0) {
            purple_debug_misc(config::pluginId, "Incoming update\n");
            td::td_api::downcast_call(*response.object, *m_updateHandler);
        } else {
            ResponseCb callback = nullptr;
            {
                std::unique_lock<std::mutex> dataLock(m_queryMutex);
                auto it = m_responseHandlers.find(response.id);
                if (it != m_responseHandlers.end()) {
                    callback = it->second;
                    m_responseHandlers.erase(it);
                } else
                    purple_debug_misc(config::pluginId, "Ignoring response to request %llu\n",
                                      (unsigned long long)response.id);
            }
            if (callback)
                (this->*callback)(response.id, std::move(response.object));
        }
    } else
        purple_debug_misc(config::pluginId, "Response id %lu timed out or something\n", response.id);
}

void PurpleTdClient::sendTdlibParameters()
{
    auto parameters = td::td_api::make_object<td::td_api::tdlibParameters>();
    const char *username = purple_account_get_username(m_account);
    parameters->database_directory_ = std::string(purple_user_dir()) + G_DIR_SEPARATOR_S +
                                      config::configSubdir + G_DIR_SEPARATOR_S + username;
    purple_debug_misc(config::pluginId, "Account %s using database directory %s\n",
                      username, parameters->database_directory_.c_str());
    parameters->use_message_database_ = true;
    parameters->use_secret_chats_ = true;
    parameters->api_id_ = 94575;
    parameters->api_hash_ = "a3406de8d171bb422bb6ddf3bbd800e2";
    parameters->system_language_code_ = "en";
    parameters->device_model_ = "Desktop";
    parameters->system_version_ = "Unknown";
    parameters->application_version_ = "1.0";
    parameters->enable_storage_optimizer_ = true;
    sendQuery(td::td_api::make_object<td::td_api::setTdlibParameters>(std::move(parameters)),
              &PurpleTdClient::authResponse);
}

void PurpleTdClient::sendPhoneNumber()
{
    const char *number = purple_account_get_username(m_account);
    sendQuery(td::td_api::make_object<td::td_api::setAuthenticationPhoneNumber>(number, nullptr),
              &PurpleTdClient::authResponse);
}

static std::string getAuthCodeDesc(const td::td_api::AuthenticationCodeType &codeType)
{
    switch (codeType.get_id()) {
    case td::td_api::authenticationCodeTypeTelegramMessage::ID:
        return "Telegram message (length: " +
               std::to_string(static_cast<const td::td_api::authenticationCodeTypeTelegramMessage &>(codeType).length_) +
               ")";
    case td::td_api::authenticationCodeTypeSms::ID:
        return "SMS (length: " +
               std::to_string(static_cast<const td::td_api::authenticationCodeTypeSms &>(codeType).length_) +
               ")";
    case td::td_api::authenticationCodeTypeCall::ID:
        return "Phone call (length: " +
               std::to_string(static_cast<const td::td_api::authenticationCodeTypeCall &>(codeType).length_) +
               ")";
    case td::td_api::authenticationCodeTypeFlashCall::ID:
        return "Poor man's phone call (pattern: " +
               static_cast<const td::td_api::authenticationCodeTypeFlashCall &>(codeType).pattern_ +
               ")";
    default:
        return "Pigeon post";
    }
}

int PurpleTdClient::requestAuthCode(gpointer user_data)
{
    PurpleTdClient *self = static_cast<PurpleTdClient *>(user_data);
    std::string message = "Enter authentication code\n";

    if (self->m_authCodeInfo) {
        if (self->m_authCodeInfo->type_)
            message += "Code sent via: " + getAuthCodeDesc(*self->m_authCodeInfo->type_) + "\n";
        if (self->m_authCodeInfo->next_type_)
            message += "Next code will be: " + getAuthCodeDesc(*self->m_authCodeInfo->next_type_) + "\n";
    }

    if (!purple_request_input (purple_account_get_connection(self->m_account),
                               (char *)"Login code",
                               message.c_str(),
                               NULL, // secondary message
                               NULL, // default value
                               FALSE, // multiline input
                               FALSE, // masked input
                               (char *)"the code",
                               (char *)"OK", G_CALLBACK(requestCodeEntered),
                               (char *)"Cancel", G_CALLBACK(requestCodeCancelled),
                               self->m_account,
                               NULL, // buddy
                               NULL, // conversation
                               self))
    {
        purple_connection_set_state (purple_account_get_connection(self->m_account), PURPLE_CONNECTED);
        PurpleConversation *conv = purple_conversation_new (PURPLE_CONV_TYPE_IM, self->m_account, "Telegram");
        purple_conversation_write (conv, "Telegram",
            "Authentication code needs to be entered but this libpurple won't cooperate",
            (PurpleMessageFlags)(PURPLE_MESSAGE_RECV | PURPLE_MESSAGE_SYSTEM), 0);
    }

    return FALSE; // This idle handler will not be called again
}

void PurpleTdClient::requestCodeEntered(PurpleTdClient *self, const gchar *code)
{
    purple_debug_misc(config::pluginId, "Authentication code entered: '%s'\n", code);
    self->sendQuery(td::td_api::make_object<td::td_api::checkAuthenticationCode>(code),
                    &PurpleTdClient::authResponse);
}

void PurpleTdClient::requestCodeCancelled(PurpleTdClient *self)
{
    purple_connection_error(purple_account_get_connection(self->m_account),
                            "Authentication code required");
}

uint64_t PurpleTdClient::sendQuery(td::td_api::object_ptr<td::td_api::Function> f, ResponseCb handler)
{
    uint64_t queryId = ++m_lastQueryId;
    purple_debug_misc(config::pluginId, "Sending query id %lu\n", (unsigned long)queryId);
    if (handler) {
        std::unique_lock<std::mutex> dataLock(m_queryMutex);
        m_responseHandlers.emplace(queryId, std::move(handler));
    }
    m_client->send({queryId, std::move(f)});
    return queryId;
}

void PurpleTdClient::authResponse(uint64_t requestId, td::td_api::object_ptr<td::td_api::Object> object)
{
    if (object->get_id() == td::td_api::error::ID) {
        td::td_api::object_ptr<td::td_api::error> error = td::move_tl_object_as<td::td_api::error>(object);
        purple_debug_misc(config::pluginId, "Authentication error on query %lu (auth step %d): code %d (%s)\n",
                          (unsigned long)requestId, (int)m_lastAuthState, (int)error->code_,
                          error->message_.c_str());
        m_authError     = std::move(error);
        g_idle_add(notifyAuthError, this);
    } else
        purple_debug_misc(config::pluginId, "Authentication success on query %lu\n", (unsigned long)requestId);
}

int PurpleTdClient::notifyAuthError(gpointer user_data)
{
    PurpleTdClient *self = static_cast<PurpleTdClient *>(user_data);

    std::string message;
    switch (self->m_lastAuthState) {
    case td::td_api::authorizationStateWaitEncryptionKey::ID:
        message = "Error applying database encryption key";
        break;
    case td::td_api::authorizationStateWaitPhoneNumber::ID:
        message = "Authentication error after sending phone number";
        break;
    default:
        message = "Authentication error";
    }

    if (self->m_authError) {
        message += ": code " + std::to_string(self->m_authError->code_) + " (" +
                self->m_authError->message_ + ")";
        self->m_authError.reset();
    }

    purple_connection_error(purple_account_get_connection(self->m_account), message.c_str());
    return FALSE; // This idle handler will not be called again
}

void PurpleTdClient::connectionReady()
{
    g_idle_add(PurpleTdClient::setPurpleConnectionReady, this);

    // td::td_api::chats response will be preceded by a string of updateNewChat and updateUser for
    // all chats and contacts, apparently even if td::td_api::getChats has limit_ of like 1
    sendQuery(td::td_api::make_object<td::td_api::getChats>(
                  nullptr, std::numeric_limits<std::int64_t>::max(), 0, 200),
              &PurpleTdClient::getChatsResponse);
}

int PurpleTdClient::setPurpleConnectionReady(gpointer user_data)
{
    purple_debug_misc(config::pluginId, "Connection ready\n");
    PurpleTdClient *self = static_cast<PurpleTdClient *>(user_data);

    purple_connection_set_state (purple_account_get_connection(self->m_account), PURPLE_CONNECTED);
    purple_blist_add_account(self->m_account);

    return FALSE; // This idle handler will not be called again
}

void PurpleTdClient::getChatsResponse(uint64_t requestId, td::td_api::object_ptr<td::td_api::Object> object)
{
    purple_debug_misc(config::pluginId, "getChats response to request %llu\n", (unsigned long long)requestId);
    if (object->get_id() == td::td_api::chats::ID) {
        td::td_api::object_ptr<td::td_api::chats> chats = td::move_tl_object_as<td::td_api::chats>(object);
        {
            TdAccountData::Lock lock(m_data);
            m_data.setActiveChats(std::move(chats->chat_ids_));
        }
        g_idle_add(updatePurpleChatList, this);
    }
}

static const char *getPurpleStatusId(const td::td_api::UserStatus &tdStatus)
{
    if (tdStatus.get_id() == td::td_api::userStatusOnline::ID)
        return purple_primitive_get_id_from_type(PURPLE_STATUS_AVAILABLE);
    else
        return purple_primitive_get_id_from_type(PURPLE_STATUS_OFFLINE);
}

int PurpleTdClient::updatePurpleChatList(gpointer user_data)
{
    PurpleTdClient *self = static_cast<PurpleTdClient *>(user_data);

    // Only populate the list from scratch
    TdAccountData::Lock lock(self->m_data);

    std::vector<PrivateChat> privateChats;
    self->m_data.getPrivateChats(privateChats);
    // lock must be held for as long as references from privateChats elements are used

    for (const PrivateChat &c: privateChats) {
        const td::td_api::chat &chat   = c.chat;
        const td::td_api::user &user   = c.user;
        const char             *userId = user.phone_number_.c_str();

        PurpleBuddy *buddy = purple_find_buddy(self->m_account, userId);
        if (buddy == NULL) {
            purple_debug_misc(config::pluginId, "Adding new buddy %s for chat id %lld\n",
                                chat.title_.c_str(), (long long)chat.id_);
            buddy = purple_buddy_new(self->m_account, userId, chat.title_.c_str());
            purple_blist_add_buddy(buddy, NULL, NULL, NULL);
        }

        purple_prpl_got_user_status(self->m_account, userId, getPurpleStatusId(*user.status_), NULL);
    }

    return FALSE; // This idle handler will not be called again
}

int PurpleTdClient::showUnreadMessages(gpointer user_data)
{
    PurpleTdClient *self = static_cast<PurpleTdClient *>(user_data);
    std::vector<UnreadChat> chats;
    TdAccountData::Lock lock(self->m_data);

    self->m_data.getUnreadChatMessages(chats);

    for (const UnreadChat &unreadChat: chats)
        if (!unreadChat.messages.empty()) {
            td::td_api::object_ptr<td::td_api::viewMessages> viewMessagesReq = td::td_api::make_object<td::td_api::viewMessages>();
            viewMessagesReq->chat_id_ = unreadChat.chatId;
            viewMessagesReq->force_read_ = true; // no idea what "closed chats" are at this point
            for (const auto &pMessage: unreadChat.messages)
                viewMessagesReq->message_ids_.push_back(pMessage->id_);
            self->sendQuery(std::move(viewMessagesReq), nullptr);

            for (const auto &pMessage: unreadChat.messages)
                self->showMessage(*pMessage);
        }

    return FALSE; // This idle handler will not be called again
}

static const char *getText(const td::td_api::message &message)
{
    if (message.content_) {
        if ((message.content_->get_id() == td::td_api::messageText::ID)) {
            const td::td_api::messageText &text = static_cast<const td::td_api::messageText &>(*message.content_);
            if (text.text_)
                return text.text_->text_.c_str();
        } else if ((message.content_->get_id() == td::td_api::messagePhoto::ID)) {
            const td::td_api::messagePhoto &photo = static_cast<const td::td_api::messagePhoto &>(*message.content_);
            if (photo.caption_)
                return photo.caption_->text_.c_str();
        }
    }
    return nullptr;
}

void PurpleTdClient::showMessage(const td::td_api::message &message)
{
    // Skip unsupported content
    const char *text = getText(message);
    if (text == nullptr) {
        purple_debug_misc(config::pluginId, "Skipping message: no supported content\n");
        return;
    }

    // m_dataMutex already locked
    const td::td_api::chat *chat = m_data.getChat(message.chat_id_);
    if (!chat) {
        purple_debug_warning(config::pluginId, "Received message with unknown chat id %lld\n",
                            (long long)message.chat_id_);
        return;
    }

    if (chat->type_->get_id() == td::td_api::chatTypePrivate::ID) {
        int32_t userId = static_cast<const td::td_api::chatTypePrivate &>(*chat->type_).user_id_;
        const td::td_api::user *user = m_data.getUser(userId);
        if (user) {
            int flags;
            if (message.is_outgoing_)
                flags = PURPLE_MESSAGE_SEND | PURPLE_MESSAGE_REMOTE_SEND;
            else
                flags = PURPLE_MESSAGE_RECV;
            serv_got_im(purple_account_get_connection(m_account), user->phone_number_.c_str(),
                        text, (PurpleMessageFlags)flags, message.date_);
        }
    }
}

void PurpleTdClient::onIncomingMessage(td::td_api::object_ptr<td::td_api::message> message)
{
    // Pass it to the main thread
    TdAccountData::Lock lock(m_data);
    m_data.addNewMessage(std::move(message));
    g_idle_add(showUnreadMessages, this);
}

int PurpleTdClient::sendMessage(const char *buddyName, const char *message)
{
    const td::td_api::user *tdUser = m_data.getUserByPhone(buddyName);
    if (tdUser == nullptr) {
        purple_debug_warning(config::pluginId, "No user with phone '%s'\n", buddyName);
        return -1;
    }
    const td::td_api::chat *tdChat = m_data.getPrivateChatByUserId(tdUser->id_);
    if (tdChat == nullptr) {
        purple_debug_warning(config::pluginId, "No chat with user %s\n", tdUser->username_.c_str());
        return -1;
    }

    td::td_api::object_ptr<td::td_api::sendMessage> send_message = td::td_api::make_object<td::td_api::sendMessage>();
    send_message->chat_id_ = tdChat->id_;
    td::td_api::object_ptr<td::td_api::inputMessageText> message_content = td::td_api::make_object<td::td_api::inputMessageText>();
    message_content->text_ = td::td_api::make_object<td::td_api::formattedText>();
    message_content->text_->text_ = message;
    send_message->input_message_content_ = std::move(message_content);

    sendQuery(std::move(send_message), nullptr);

    // Message shall not be echoed: tdlib will shortly present it as a new message and it will be displayed then
    return 0;
}
