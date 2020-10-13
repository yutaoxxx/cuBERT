#include <algorithm>
#include <string>
#include <vector>
#include <stdexcept>

#include "cuBERT.h"
#include "cuBERT/common.h"
#include "cuBERT/tokenization.h"
#include "cuBERT/BertM.h"
//初始化
void cuBERT_initialize() {
    cuBERT::initialize();
}

void cuBERT_finalize() {
    cuBERT::finalize();
}
//获得gpu的数量
int cuBERT_get_gpu_count() {
    return cuBERT::get_gpu_count();
}
// 打开一个bert，返回一个bert对象
// model_file  模型文件
// max_batch_size      最大的batch_size
// seq_length          序列长度
// num_hidden_layers   bert的层数
// num_attention_heads attention头的个数
// cuBERT_ComputerType bert计算的类别
void *cuBERT_open(const char *model_file,
                  int max_batch_size,
                  int seq_length,
                  int num_hidden_layers,
                  int num_attention_heads,
                  cuBERT_ComputeType compute_type) {
    if (compute_type == cuBERT_COMPUTE_FLOAT) {
        //非半精度返回 cuBERT::BertM<float>对象
        return new cuBERT::BertM<float>(model_file,
                                        max_batch_size,
                                        seq_length,
                                        num_hidden_layers, num_attention_heads);
    } else {
#ifdef HAVE_CUDA
        //有CUDA的时候 返回 cuBERT::BertM<half>对象
        return new cuBERT::BertM<half>(model_file,
                                       max_batch_size,
                                       seq_length,
                                       num_hidden_layers, num_attention_heads);
#else
        //没有CUDA的时候  抛出异常，半精度不支持CPU
        throw std::invalid_argument("half precision not supported by CPU");
#endif
    }
}

/* bert正向传播计算
 * model    模型的指针
 * batch_size    batch_size
 * input_ids     输入的ids           int指针
 * input_mask    输入的mask          int8_t指针
 * segment_ids   输入的segment_ids   int8_t指针
 * output        输出序列             void指针
 */
void cuBERT_compute(void *model,
                    int batch_size,
                    int *input_ids,
                    int8_t *input_mask,
                    int8_t *segment_ids,
                    void *output,
                    cuBERT_OutputType output_type,
                    cuBERT_ComputeType compute_type) {
    if (compute_type == cuBERT_COMPUTE_FLOAT) {
        //调用cuBERT::BertM里面的compute函数
        ((cuBERT::BertM<float> *) model)->compute(batch_size,
                                                  input_ids, input_mask, segment_ids,
                                                  (float*) output, output_type);
    } else {
#ifdef HAVE_CUDA
        ((cuBERT::BertM<half> *) model)->compute(batch_size,
                                                 input_ids, input_mask, segment_ids,
                                                 (half*) output, output_type);
#else
        throw std::invalid_argument("half precision not supported by CPU");
#endif
    }
}

void cuBERT_compute_m(void* model,
                      int batch_size,
                      int* input_ids,
                      int8_t* input_mask,
                      int8_t* segment_ids,
                      cuBERT_Output* output,
                      cuBERT_ComputeType compute_type,
                      int output_to_float) {
    if (compute_type == cuBERT_COMPUTE_FLOAT) {
        // 传参不同  output_to_float
        ((cuBERT::BertM<float> *) model)->compute(batch_size,
                                                  input_ids, input_mask, segment_ids,
                                                  output, output_to_float);
    } else {
#ifdef HAVE_CUDA
        ((cuBERT::BertM<half> *) model)->compute(batch_size,
                                                 input_ids, input_mask, segment_ids,
                                                 output, output_to_float);
#else
        throw std::invalid_argument("half precision not supported by CPU");
#endif
    }
}
/* 释放bertmodel的内存
 *
 */
void cuBERT_close(void *model, cuBERT_ComputeType compute_type) {
    if (compute_type == cuBERT_COMPUTE_FLOAT) {
        //直接删除这个指针
        delete (cuBERT::BertM<float> *) model;
    } else {
#ifdef HAVE_CUDA
        delete (cuBERT::BertM<half> *) model;
#else
        throw std::invalid_argument("half precision not supported by CPU");
#endif
    }
}
// 返回一个tokenizer分词器
void* cuBERT_open_tokenizer(const char* vocab_file, int do_lower_case) {
    return new cuBERT::FullTokenizer(vocab_file, do_lower_case);
}
// 释放掉分词器
void cuBERT_close_tokenizer(void* tokenizer) {
    delete (cuBERT::FullTokenizer *) tokenizer;
}

/**
 * Truncates a sequence pair in place to the maximum length.
 * @param tokens_a
 * @param tokens_a
 * @param max_length
 */
void _truncate_seq_pair(std::vector<std::string>* tokens_a,
                        std::vector<std::string>* tokens_b,
                        size_t max_length) {
// This is a simple heuristic which will always truncate the longer sequence
// one token at a time. This makes more sense than truncating an equal percent
// of tokens from each, since if one sequence is very short then each token
// that's truncated likely contains more information than a longer sequence.
    while (true) {
        size_t total_length = tokens_a->size() + tokens_b->size();
        if (total_length <= max_length) {
            break;
        }
        if (tokens_a->size() > tokens_b->size()) {
            tokens_a->pop_back();
        } else {
            tokens_b->pop_back();
        }
    }
}

/**
 * Converts a single `InputExample` into a single `InputFeatures`.
 */
void convert_single_example(cuBERT::FullTokenizer* tokenizer,
                            size_t max_seq_length,
                            const char* text_a, const char* text_b,
                            int *input_ids, int8_t *input_mask, int8_t *segment_ids) {
    std::vector<std::string> tokens_a;
    tokens_a.reserve(max_seq_length);

    std::vector<std::string> tokens_b;
    tokens_b.reserve(max_seq_length);

    tokenizer->tokenize(text_a, &tokens_a, max_seq_length);
    if (text_b != nullptr) {
        tokenizer->tokenize(text_b, &tokens_b, max_seq_length);

        // Modifies `tokens_a` and `tokens_b` in place so that the total
        // length is less than the specified length.
        // Account for [CLS], [SEP], [SEP] with "- 3"
        _truncate_seq_pair(&tokens_a, &tokens_b, max_seq_length - 3);
    } else {
        if (tokens_a.size() > max_seq_length - 2) {
            tokens_a.resize(max_seq_length - 2);
        }
    }

    // The convention in BERT is:
    // (a) For sequence pairs:
    //  tokens:   [CLS] is this jack ##son ##ville ? [SEP] no it is not . [SEP]
    //  type_ids: 0     0  0    0    0     0       0 0     1  1  1  1   1 1
    // (b) For single sequences:
    //  tokens:   [CLS] the dog is hairy . [SEP]
    //  type_ids: 0     0   0   0  0     0 0
    //
    // Where "type_ids" are used to indicate whether this is the first
    // sequence or the second sequence. The embedding vectors for `type=0` and
    // `type=1` were learned during pre-training and are added to the wordpiece
    // embedding vector (and position vector). This is not *strictly* necessary
    // since the [SEP] token unambiguously separates the sequences, but it makes
    // it easier for the model to learn the concept of sequences.
    //
    // For classification tasks, the first vector (corresponding to [CLS]) is
    // used as as the "sentence vector". Note that this only makes sense because
    // the entire model is fine-tuned.
    input_ids[0] = tokenizer->convert_token_to_id("[CLS]");
    segment_ids[0] = 0;
    for (int i = 0; i < tokens_a.size(); ++i) {
        input_ids[i + 1] = tokenizer->convert_token_to_id(tokens_a[i]);
        segment_ids[i + 1] = 0;
    }
    input_ids[tokens_a.size() + 1] = tokenizer->convert_token_to_id("[SEP]");
    segment_ids[tokens_a.size() + 1] = 0;

    if (text_b != nullptr) {
        for (int i = 0; i < tokens_b.size(); ++i) {
            input_ids[i + tokens_a.size() + 2] = tokenizer->convert_token_to_id(tokens_b[i]);
            segment_ids[i + tokens_a.size() + 2] = 1;
        }
        input_ids[tokens_b.size() + tokens_a.size() + 2] = tokenizer->convert_token_to_id("[SEP]");
        segment_ids[tokens_b.size() + tokens_a.size() + 2] = 1;
    }

    size_t len = text_b != nullptr ? tokens_a.size() + tokens_b.size() + 3 : tokens_a.size() + 2;
    std::fill_n(input_mask, len, 1);

    // Zero-pad up to the sequence length.
    std::fill_n(input_ids + len, max_seq_length - len, 0);
    std::fill_n(input_mask + len, max_seq_length - len, 0);
    std::fill_n(segment_ids + len, max_seq_length - len, 0);
}

void cuBERT_tokenize_compute(void* model,
                             void* tokenizer,
                             int batch_size,
                             const char** text_a,
                             const char** text_b,
                             void* output,
                             cuBERT_OutputType output_type,
                             cuBERT_ComputeType compute_type) {
    size_t max_seq_length;
    if (compute_type == cuBERT_COMPUTE_FLOAT) {
        max_seq_length = ((cuBERT::BertM<float> *) model)->seq_length;
    } else {
#ifdef HAVE_CUDA
        max_seq_length = ((cuBERT::BertM<half> *) model)->seq_length;
#else
        throw std::invalid_argument("half precision not supported by CPU");
#endif
    }

    int input_ids[batch_size * max_seq_length];
    int8_t input_mask[batch_size * max_seq_length];
    int8_t segment_ids[batch_size * max_seq_length];

    for (int batch_idx = 0; batch_idx < batch_size; ++batch_idx) {
        convert_single_example((cuBERT::FullTokenizer *) tokenizer,
                               max_seq_length,
                               text_a[batch_idx],
                               text_b == nullptr ? nullptr : text_b[batch_idx],
                               input_ids + max_seq_length * batch_idx,
                               input_mask + max_seq_length * batch_idx,
                               segment_ids + max_seq_length * batch_idx);
    }

    cuBERT_compute(model, batch_size, input_ids, input_mask, segment_ids, output, output_type, compute_type);
}

void cuBERT_tokenize_compute_m(void* model,
                               void* tokenizer,
                               int batch_size,
                               const char** text_a,
                               const char** text_b,
                               cuBERT_Output* output,
                               cuBERT_ComputeType compute_type,
                               int output_to_float) {
    size_t max_seq_length;
    if (compute_type == cuBERT_COMPUTE_FLOAT) {
        max_seq_length = ((cuBERT::BertM<float> *) model)->seq_length;
    } else {
#ifdef HAVE_CUDA
        max_seq_length = ((cuBERT::BertM<half> *) model)->seq_length;
#else
        throw std::invalid_argument("half precision not supported by CPU");
#endif
    }

    int input_ids[batch_size * max_seq_length];
    int8_t input_mask[batch_size * max_seq_length];
    int8_t segment_ids[batch_size * max_seq_length];

    for (int batch_idx = 0; batch_idx < batch_size; ++batch_idx) {
        convert_single_example((cuBERT::FullTokenizer *) tokenizer,
                               max_seq_length,
                               text_a[batch_idx],
                               text_b == nullptr ? nullptr : text_b[batch_idx],
                               input_ids + max_seq_length * batch_idx,
                               input_mask + max_seq_length * batch_idx,
                               segment_ids + max_seq_length * batch_idx);
    }

    cuBERT_compute_m(model, batch_size, input_ids, input_mask, segment_ids, output, compute_type, output_to_float);
}
