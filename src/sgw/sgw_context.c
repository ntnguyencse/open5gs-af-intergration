#define TRACE_MODULE _sgw_context

#include "core_debug.h"
#include "core_pool.h"
#include "core_index.h"
#include "core_jsmn.h"
#include "core_lib.h"

#include "types.h"
#include "gtp_path.h"

#include "context.h"
#include "sgw_context.h"

static sgw_context_t self;

index_declare(sgw_ue_pool, sgw_ue_t, MAX_POOL_OF_UE);
index_declare(sgw_sess_pool, sgw_sess_t, MAX_POOL_OF_SESS);
index_declare(sgw_bearer_pool, sgw_bearer_t, MAX_POOL_OF_BEARER);

static int context_initialized = 0;

status_t sgw_context_init()
{
    d_assert(context_initialized == 0, return CORE_ERROR,
            "SGW context already has been initialized");

    memset(&self, 0, sizeof(sgw_context_t));

    index_init(&sgw_ue_pool, MAX_POOL_OF_UE);
    index_init(&sgw_sess_pool, MAX_POOL_OF_SESS);
    index_init(&sgw_bearer_pool, MAX_POOL_OF_BEARER);

    list_init(&self.s11_node.local_list);
    list_init(&self.s11_node.remote_list);

    list_init(&self.s5c_node.local_list);
    list_init(&self.s5c_node.remote_list);

    self.imsi_ue_hash = hash_make();

    context_initialized = 1;

    return CORE_OK;
}

status_t sgw_context_final()
{
    d_assert(context_initialized == 1, return CORE_ERROR,
            "SGW context already has been finalized");

    gtp_xact_delete_all(&self.s11_node);
    gtp_xact_delete_all(&self.s5c_node);

    sgw_ue_remove_all();

    d_assert(self.imsi_ue_hash, , "Null param");
    hash_destroy(self.imsi_ue_hash);

    index_final(&sgw_bearer_pool);
    index_final(&sgw_sess_pool);
    index_final(&sgw_ue_pool);

    context_initialized = 0;
    
    return CORE_OK;
}

sgw_context_t* sgw_self()
{
    return &self;
}

static status_t sgw_context_prepare()
{
    self.s11_port = GTPV2_C_UDP_PORT;
    self.s11_node.port = GTPV2_C_UDP_PORT;

    self.s5c_port = GTPV2_C_UDP_PORT;
    self.s5c_node.port = GTPV2_C_UDP_PORT;

    self.s1u_port = GTPV1_U_UDP_PORT;
    self.s5u_port = GTPV1_U_UDP_PORT;

    return CORE_OK;
}

static status_t sgw_context_validation()
{
    if (self.s11_node.addr == 0)
    {
        d_error("No MME.NEWORK.S11_ADDR in '%s'",
                context_self()->config.path);
        return CORE_ERROR;
    }
    if (self.s5c_node.addr == 0)
    {
        d_error("No PGW.NEWORK.S5C_ADDR in '%s'",
                context_self()->config.path);
        return CORE_ERROR;
    }
    if (self.s11_addr == 0)
    {
        d_error("No SGW.NEWORK.S11_ADDR in '%s'",
                context_self()->config.path);
        return CORE_ERROR;
    }
    if (self.s5c_addr == 0)
    {
        d_error("No SGW.NEWORK.S5C_ADDR in '%s'",
                context_self()->config.path);
        return CORE_ERROR;
    }
    if (self.s1u_addr == 0)
    {
        d_error("No SGW.NEWORK.S1U_ADDR in '%s'",
                context_self()->config.path);
        return CORE_ERROR;
    }
    if (self.s5u_addr == 0)
    {
        d_error("No SGW.NEWORK.S5U_ADDR in '%s'",
                context_self()->config.path);
        return CORE_ERROR;
    }
    return CORE_OK;
}

status_t sgw_context_parse_config()
{
    status_t rv;
    config_t *config = &context_self()->config;

    char *json = config->json;
    jsmntok_t *token = config->token;

    typedef enum {
        START, ROOT,
        MME_START, MME_ROOT,
        SGW_START, SGW_ROOT,
        PGW_START, PGW_ROOT,
        SKIP, STOP
    } parse_state;
    parse_state state = START;
    parse_state stack = STOP;

    size_t root_tokens = 0;
    size_t mme_tokens = 0;
    size_t sgw_tokens = 0;
    size_t pgw_tokens = 0;
    size_t skip_tokens = 0;
    int i, j, m, n;
    int arr, size;

    rv = sgw_context_prepare();
    if (rv != CORE_OK) return rv;

    for (i = 0, j = 1; j > 0; i++, j--)
    {
        jsmntok_t *t = &token[i];

        j += t->size;

        switch (state)
        {
            case START:
            {
                state = ROOT;
                root_tokens = t->size;

                break;
            }
            case ROOT:
            {
                if (jsmntok_equal(json, t, "MME") == 0)
                {
                    state = MME_START;
                }
                else if (jsmntok_equal(json, t, "SGW") == 0)
                {
                    state = SGW_START;
                }
                else if (jsmntok_equal(json, t, "PGW") == 0)
                {
                    state = PGW_START;
                }
                else
                {
                    state = SKIP;
                    stack = ROOT;
                    skip_tokens = t->size;

                    root_tokens--;
                    if (root_tokens == 0) state = STOP;
                }
                break;
            }
            case MME_START:
            {
                state = MME_ROOT;
                mme_tokens = t->size;

                break;
            }
            case MME_ROOT:
            {
                if (jsmntok_equal(json, t, "NETWORK") == 0)
                {
                    m = 1;
                    size = 1;

                    if ((t+1)->type == JSMN_ARRAY)
                    {
                        m = 2;
                    }

                    for (arr = 0; arr < size; arr++)
                    {
                        for (n = 1; n > 0; m++, n--)
                        {
                            n += (t+m)->size;

                            if (jsmntok_equal(json, t+m, "S11_ADDR") == 0)
                            {
                                char *v = jsmntok_to_string(json, t+m+1);
                                if (v) self.s11_node.addr = inet_addr(v);
                            }
                            else if (jsmntok_equal(json, t+m, "S11_PORT") == 0)
                            {
                                char *v = jsmntok_to_string(json, t+m+1);
                                if (v) self.s11_node.port = atoi(v);
                            }
                        }
                    }
                }

                state = SKIP;
                stack = MME_ROOT;
                skip_tokens = t->size;

                mme_tokens--;
                if (mme_tokens == 0) stack = ROOT;
                break;
            }
            case SGW_START:
            {
                state = SGW_ROOT;
                sgw_tokens = t->size;

                break;
            }
            case SGW_ROOT:
            {
                if (jsmntok_equal(json, t, "NETWORK") == 0)
                {
                    m = 1;
                    size = 1;

                    if ((t+1)->type == JSMN_ARRAY)
                    {
                        m = 2;
                    }

                    for (arr = 0; arr < size; arr++)
                    {
                        for (n = 1; n > 0; m++, n--)
                        {
                            n += (t+m)->size;

                            if (jsmntok_equal(json, t+m, "S11_ADDR") == 0)
                            {
                                char *v = jsmntok_to_string(json, t+m+1);
                                if (v) self.s11_addr = inet_addr(v);
                            }
                            else if (jsmntok_equal(json, t+m, "S11_PORT") == 0)
                            {
                                char *v = jsmntok_to_string(json, t+m+1);
                                if (v) self.s11_port = atoi(v);
                            }
                            else if (jsmntok_equal(json, t+m, "S5C_ADDR") == 0)
                            {
                                char *v = jsmntok_to_string(json, t+m+1);
                                if (v) self.s5c_addr = inet_addr(v);
                            }
                            else if (jsmntok_equal(json, t+m, "S5C_PORT") == 0)
                            {
                                char *v = jsmntok_to_string(json, t+m+1);
                                if (v) self.s5c_port = atoi(v);
                            }
                            else if (jsmntok_equal(json, t+m, "S1U_ADDR") == 0)
                            {
                                char *v = jsmntok_to_string(json, t+m+1);
                                if (v) self.s1u_addr = inet_addr(v);
                            }
                            else if (jsmntok_equal(json, t+m, "S1U_PORT") == 0)
                            {
                                char *v = jsmntok_to_string(json, t+m+1);
                                if (v) self.s1u_port = atoi(v);
                            }
                            else if (jsmntok_equal(json, t+m, "S5U_ADDR") == 0)
                            {
                                char *v = jsmntok_to_string(json, t+m+1);
                                if (v) self.s5u_addr = inet_addr(v);
                            }
                            else if (jsmntok_equal(json, t+m, "S5U_PORT") == 0)
                            {
                                char *v = jsmntok_to_string(json, t+m+1);
                                if (v) self.s5u_port = atoi(v);
                            }
                        }
                    }
                }

                state = SKIP;
                stack = SGW_ROOT;
                skip_tokens = t->size;

                sgw_tokens--;
                if (sgw_tokens == 0) stack = ROOT;
                break;
            }
            case PGW_START:
            {
                state = PGW_ROOT;
                pgw_tokens = t->size;

                break;
            }
            case PGW_ROOT:
            {
                if (jsmntok_equal(json, t, "NETWORK") == 0)
                {
                    m = 1;
                    size = 1;

                    if ((t+1)->type == JSMN_ARRAY)
                    {
                        m = 2;
                    }

                    for (arr = 0; arr < size; arr++)
                    {
                        for (n = 1; n > 0; m++, n--)
                        {
                            n += (t+m)->size;

                            if (jsmntok_equal(json, t+m, "S5C_ADDR") == 0)
                            {
                                char *v = jsmntok_to_string(json, t+m+1);
                                if (v) self.s5c_node.addr = inet_addr(v);
                            }
                            else if (jsmntok_equal(json, t+m, "S5C_PORT") == 0)
                            {
                                char *v = jsmntok_to_string(json, t+m+1);
                                if (v) self.s5c_node.port = atoi(v);
                            }
                        }
                    }
                }

                state = SKIP;
                stack = PGW_ROOT;
                skip_tokens = t->size;

                pgw_tokens--;
                if (pgw_tokens == 0) stack = ROOT;
                break;
            }
            case SKIP:
            {
                skip_tokens += t->size;

                skip_tokens--;
                if (skip_tokens == 0) state = stack;
                break;
            }
            case STOP:
            {
                break;
            }
            default:
            {
                d_error("Failed to parse configuration in the state(%u)", 
                        state);
                break;
            }

        }
    }

    rv = sgw_context_validation();
    if (rv != CORE_OK) return rv;

    return CORE_OK;
}

status_t sgw_context_setup_trace_module()
{
    int gtp = context_self()->trace_level.gtp;
    int others = context_self()->trace_level.others;

    if (gtp)
    {
        extern int _sgw_sm;
        d_trace_level(&_sgw_sm, gtp);
        extern int _sgw_s11_handler;
        d_trace_level(&_sgw_s11_handler, gtp);
        extern int _sgw_s11_handler;
        d_trace_level(&_sgw_s11_handler, gtp);
        extern int _gtp_path;
        d_trace_level(&_gtp_path, gtp);
        extern int _sgw_path;
        d_trace_level(&_sgw_path, gtp);
        extern int _tlv_msg;
        d_trace_level(&_tlv_msg, gtp);
        extern int _gtp_xact;
        d_trace_level(&_gtp_xact, gtp);
    }

    if (others)
    {
        extern int _mutex;
        d_trace_level(&_mutex, others);
        extern int _pkbuf;
        d_trace_level(&_pkbuf, others);

        extern int _context;
        d_trace_level(&_context, others);
        extern int _sgw_context;
        d_trace_level(&_sgw_context, others);
    }

    return CORE_OK;
}

sgw_ue_t* sgw_ue_add(
        c_uint8_t *imsi, int imsi_len, c_int8_t *apn, c_uint8_t ebi)
{
    sgw_ue_t *sgw_ue = NULL;
    sgw_sess_t *sess = NULL;

    index_alloc(&sgw_ue_pool, &sgw_ue);
    d_assert(sgw_ue, return NULL, "Null param");

    sgw_ue->sgw_s11_teid = sgw_ue->index;
    sgw_ue->sgw_s11_addr = sgw_self()->s11_addr;

    /* Set IMSI */
    sgw_ue->imsi_len = imsi_len;
    memcpy(sgw_ue->imsi, imsi, sgw_ue->imsi_len);
#if 0
    core_buffer_to_bcd(sgw_ue->imsi, sgw_ue->imsi_len, sgw_ue->imsi_bcd);
#endif

    list_init(&sgw_ue->sess_list);

    sess = sgw_sess_add(sgw_ue, apn, ebi);
    d_assert(sess, index_free(&sgw_ue_pool, sgw_ue); return NULL, "Null param");

    hash_set(self.imsi_ue_hash, sgw_ue->imsi, sgw_ue->imsi_len, sgw_ue);

    return sgw_ue;
}

status_t sgw_ue_remove(sgw_ue_t *sgw_ue)
{
    d_assert(sgw_ue, return CORE_ERROR, "Null param");

    /* Clear hash table */
    if (sgw_ue->imsi_len != 0)
        hash_set(self.imsi_ue_hash, sgw_ue->imsi, sgw_ue->imsi_len, NULL);
    
    sgw_sess_remove_all(sgw_ue);

    index_free(&sgw_ue_pool, sgw_ue);

    return CORE_OK;
}

status_t sgw_ue_remove_all()
{
    hash_index_t *hi = NULL;
    sgw_ue_t *sgw_ue = NULL;

    for (hi = sgw_ue_first(); hi; hi = sgw_ue_next(hi))
    {
        sgw_ue = sgw_ue_this(hi);
        sgw_ue_remove(sgw_ue);
    }

    return CORE_OK;
}

sgw_ue_t* sgw_ue_find(index_t index)
{
    d_assert(index, return NULL, "Invalid index = 0x%x", index);
    return index_find(&sgw_ue_pool, index);
}

sgw_ue_t* sgw_ue_find_by_imsi_bcd(c_int8_t *imsi_bcd)
{
    c_uint8_t imsi[MAX_IMSI_LEN];
    int imsi_len = 0;

    d_assert(imsi_bcd, return NULL,"Invalid param");

    core_bcd_to_buffer(imsi_bcd, imsi, &imsi_len);

    return sgw_ue_find_by_imsi(imsi, imsi_len);
}

sgw_ue_t* sgw_ue_find_by_imsi(c_uint8_t *imsi, int imsi_len)
{
    d_assert(imsi && imsi_len, return NULL,"Invalid param");

    return (sgw_ue_t *)hash_get(self.imsi_ue_hash, imsi, imsi_len);
}

sgw_ue_t* sgw_ue_find_by_teid(c_uint32_t teid)
{
    return sgw_ue_find(teid);
}

sgw_ue_t *sgw_ue_find_or_add_by_message(gtp_message_t *gtp_message)
{
    sgw_ue_t *sgw_ue = NULL;

    gtp_create_session_request_t *req = &gtp_message->create_session_request;

    if (req->sender_f_teid_for_control_plane.presence == 0)
    {
        d_error("No IMSI");
        return NULL;
    }

    if (req->access_point_name.presence == 0)
    {
        d_error("No APN");
        return NULL;
    }

    sgw_ue = sgw_ue_find_by_imsi(req->imsi.data, req->imsi.len);
    if (!sgw_ue)
    {
        c_int8_t apn[MAX_APN_LEN];
        apn_parse(apn, req->access_point_name.data, req->access_point_name.len);
        sgw_ue = sgw_ue_add(req->imsi.data, req->imsi.len, apn,
            req->bearer_contexts_to_be_created.eps_bearer_id.u8);
        d_assert(sgw_ue, return NULL, "No UE Context");
    }

    return sgw_ue;
}

hash_index_t *sgw_ue_first()
{
    d_assert(self.imsi_ue_hash, return NULL, "Null param");
    return hash_first(self.imsi_ue_hash);
}

hash_index_t *sgw_ue_next(hash_index_t *hi)
{
    return hash_next(hi);
}

sgw_ue_t *sgw_ue_this(hash_index_t *hi)
{
    d_assert(hi, return NULL, "Null param");
    return hash_this_val(hi);
}

sgw_sess_t *sgw_sess_add(
        sgw_ue_t *sgw_ue, c_int8_t *apn, c_uint8_t ebi)
{
    sgw_sess_t *sess = NULL;
    sgw_bearer_t *bearer = NULL;

    d_assert(sgw_ue, return NULL, "Null param");
    d_assert(ebi, return NULL, "Invalid EBI(%d)", ebi);

    index_alloc(&sgw_sess_pool, &sess);
    d_assert(sess, return NULL, "Null param");

    sess->sgw_s5c_teid = sess->index;
    sess->sgw_s5c_addr = sgw_self()->s5c_addr;

    /* Set APN */
    core_cpystrn(sess->pdn.apn, apn, MAX_APN_LEN+1);

    list_init(&sess->bearer_list);
    list_append(&sgw_ue->sess_list, sess);

    sess->sgw_ue = sgw_ue;

    bearer = sgw_bearer_add(sess, ebi);
    d_assert(bearer, sgw_sess_remove(sess); return NULL, 
            "Can't add default bearer context");

    return sess;
}

status_t sgw_sess_remove(sgw_sess_t *sess)
{
    d_assert(sess, return CORE_ERROR, "Null param");
    d_assert(sess->sgw_ue, return CORE_ERROR, "Null param");

    sgw_bearer_remove_all(sess);

    list_remove(&sess->sgw_ue->sess_list, sess);
    index_free(&sgw_sess_pool, sess);

    return CORE_OK;
}

status_t sgw_sess_remove_all(sgw_ue_t *sgw_ue)
{
    sgw_sess_t *sess = NULL, *next_sess = NULL;
    
    sess = sgw_sess_first(sgw_ue);
    while (sess)
    {
        next_sess = sgw_sess_next(sess);

        sgw_sess_remove(sess);

        sess = next_sess;
    }

    return CORE_OK;
}

sgw_sess_t* sgw_sess_find(index_t index)
{
    d_assert(index, return NULL, "Invalid Index");
    return index_find(&sgw_sess_pool, index);
}

sgw_sess_t* sgw_sess_find_by_teid(c_uint32_t teid)
{
    return sgw_sess_find(teid);
}

sgw_sess_t* sgw_sess_find_by_apn(sgw_ue_t *sgw_ue, c_int8_t *apn)
{
    sgw_sess_t *sess = NULL;
    
    sess = sgw_sess_first(sgw_ue);
    while (sess)
    {
        if (strcmp(sess->pdn.apn, apn) == 0)
            return sess;

        sess = sgw_sess_next(sess);
    }

    return NULL;
}

sgw_sess_t* sgw_sess_find_by_ebi(sgw_ue_t *sgw_ue, c_uint8_t ebi)
{
    sgw_bearer_t *bearer = NULL;

    bearer = sgw_bearer_find_by_ue_ebi(sgw_ue, ebi);
    if (bearer)
        return bearer->sess;

    return NULL;
}

sgw_sess_t* sgw_sess_first(sgw_ue_t *sgw_ue)
{
    return list_first(&sgw_ue->sess_list);
}

sgw_sess_t* sgw_sess_next(sgw_sess_t *sess)
{
    return list_next(sess);
}

sgw_bearer_t* sgw_bearer_add(sgw_sess_t *sess, c_uint8_t ebi)
{
    sgw_bearer_t *bearer = NULL;

    d_assert(sess, return NULL, "Null param");

    index_alloc(&sgw_bearer_pool, &bearer);
    d_assert(bearer, return NULL, "Bearer context allocation failed");

    bearer->ebi = ebi;
    bearer->sgw_s1u_teid = bearer->index;
    bearer->sgw_s1u_addr = sgw_self()->s1u_addr;
    bearer->sgw_s5u_teid = bearer->index;
    bearer->sgw_s5u_addr = sgw_self()->s5u_addr;
    
    bearer->sess = sess;
    list_append(&sess->bearer_list, bearer);

    return bearer;
}

status_t sgw_bearer_remove(sgw_bearer_t *bearer)
{
    int i;

    d_assert(bearer, return CORE_ERROR, "Null param");
    d_assert(bearer->sess, return CORE_ERROR, "Null param");

    /* Free the buffered packets */
    for (i = 0; i < bearer->num_buffered_pkt; i++)
        pkbuf_free(bearer->buffered_pkts[i]);

    list_remove(&bearer->sess->bearer_list, bearer);
    index_free(&sgw_bearer_pool, bearer);

    return CORE_OK;
}

status_t sgw_bearer_remove_all(sgw_sess_t *sess)
{
    sgw_bearer_t *bearer = NULL, *next_bearer = NULL;

    d_assert(sess, return CORE_ERROR, "Null param");
    
    bearer = list_first(&sess->bearer_list);
    while (bearer)
    {
        next_bearer = list_next(bearer);

        sgw_bearer_remove(bearer);

        bearer = next_bearer;
    }

    return CORE_OK;
}

sgw_bearer_t* sgw_bearer_find(index_t index)
{
    d_assert(index && index < MAX_POOL_OF_BEARER, return NULL, 
            "Invalid Index(%d)",index);

    return index_find(&sgw_bearer_pool, index);
}

sgw_bearer_t* sgw_bearer_find_by_sgw_s5u_teid(c_uint32_t sgw_s5u_teid)
{
    return sgw_bearer_find(sgw_s5u_teid);
}

sgw_bearer_t* sgw_bearer_find_by_sgw_s1u_teid(c_uint32_t sgw_s1u_teid)
{
    return sgw_bearer_find(sgw_s1u_teid);
}

sgw_bearer_t* sgw_bearer_find_by_sess_ebi(sgw_sess_t *sess, c_uint8_t ebi)
{
    sgw_bearer_t *bearer = NULL;

    bearer = sgw_bearer_first(sess);
    while(bearer)
    {
        if (ebi == bearer->ebi)
            return bearer;

        bearer = sgw_bearer_next(bearer);
    }

    return NULL;
}

sgw_bearer_t* sgw_bearer_find_by_ue_ebi(sgw_ue_t *sgw_ue, c_uint8_t ebi)
{
    sgw_sess_t *sess = NULL;
    sgw_bearer_t *bearer = NULL;
    
    sess = sgw_sess_first(sgw_ue);
    while (sess)
    {
        bearer = sgw_bearer_find_by_sess_ebi(sess, ebi);
        if (bearer)
        {
            return bearer;
        }

        sess = sgw_sess_next(sess);
    }

    return NULL;
}

sgw_bearer_t* sgw_default_bearer_in_sess(sgw_sess_t *sess)
{
    return sgw_bearer_first(sess);
}

sgw_bearer_t* sgw_bearer_first(sgw_sess_t *sess)
{
    d_assert(sess, return NULL, "Null param");
    return list_first(&sess->bearer_list);
}

sgw_bearer_t* sgw_bearer_next(sgw_bearer_t *bearer)
{
    return list_next(bearer);
}

