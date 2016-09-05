#pragma once

#include "../mblas/matrix.h"
#include "model.h"
#include "gru.h"

class Decoder {
  private:
    template <class Weights>
    class Embeddings {
      public:
        Embeddings(const Weights& model);
            
        void Lookup(mblas::Matrix& Rows, const std::vector<size_t>& ids);
        
        size_t GetCols();

        size_t GetRows() const;

      private:
        const Weights& w_;
    };
    
    //////////////////////////////////////////////////////////////
    template <class Weights1, class Weights2>
    class RNNHidden {
      public:
        RNNHidden(const Weights1& initModel, const Weights2& gruModel);
        
        void InitializeState(mblas::Matrix& State,
                             const mblas::Matrix& SourceContext,
                             const size_t batchSize = 1);
        
        void GetNextState(mblas::Matrix& NextState,
                          const mblas::Matrix& State,
                          const mblas::Matrix& Context);

      private:
        const Weights1& w_;
        const GRU<Weights2> gru_;
        
        mblas::Matrix Temp1_;
        mblas::Matrix Temp2_;
    };
    
    //////////////////////////////////////////////////////////////
    template <class Weights>
    class RNNFinal {
      public:
        RNNFinal(const Weights& model)
        : gru_(model) {}          
        
        void GetNextState(mblas::Matrix& NextState,
                          const mblas::Matrix& State,
                          const mblas::Matrix& Context) {
          gru_.GetNextState(NextState, State, Context);
        }
        
      private:
        const GRU<Weights> gru_;
    };
        
    //////////////////////////////////////////////////////////////
    template <class Weights>
    class Attention {
      public:
        Attention(const Weights& model)
        : w_(model)
        {
          V_ = blaze::trans(blaze::row(w_.V_, 0));  
        }
          
        void GetAlignedSourceContext(mblas::Matrix& AlignedSourceContext,
                                     const mblas::Matrix& HiddenState,
                                     const mblas::Matrix& SourceContext) {
          using namespace mblas;  
          
          Temp1_ = SourceContext * w_.U_;
          Temp2_ = HiddenState * w_.W_;
          AddBiasVector<byRow>(Temp2_, w_.B_);
          
          // For batching: create an A across different sentences,
          // maybe by mapping and looping. In the and join different
          // alignment matrices into one
          // Or masking?
          Temp1_ = Broadcast<Matrix>(Tanh(), Temp1_, Temp2_);
          
          A_.resize(Temp1_.rows(), 1);
          blaze::column(A_, 0) = Temp1_ * V_;
          size_t words = SourceContext.rows();
          // batch size, for batching, divide by numer of sentences
          size_t batchSize = HiddenState.rows();
          Reshape(A_, batchSize, words); // due to broadcasting above
          
          float bias = w_.C_(0,0);
          blaze::forEach(A_, [=](float x) { return x + bias; });
          
          mblas::Softmax(A_);
          AlignedSourceContext = A_ * SourceContext;
        }
        
        void GetAttention(mblas::Matrix& Attention) {
          Attention = A_;
        }
      
      private:
        const Weights& w_;
        
        mblas::Matrix Temp1_;
        mblas::Matrix Temp2_;
        mblas::Matrix A_;
        mblas::ColumnVector V_;
    };
    
    //////////////////////////////////////////////////////////////
    template <class Weights>
    class Softmax {
      public:
        Softmax(const Weights& model)
        : w_(model),
        filtered_(false)
        {}
          
        void GetProbs(mblas::ArrayMatrix& Probs,
                  const mblas::Matrix& State,
                  const mblas::Matrix& Embedding,
                  const mblas::Matrix& AlignedSourceContext) {
          using namespace mblas;
          
          T1_ = State * w_.W1_;
          T2_ = Embedding * w_.W2_;
          T3_ = AlignedSourceContext * w_.W3_;
          
          AddBiasVector<byRow>(T1_, w_.B1_);
          AddBiasVector<byRow>(T2_, w_.B2_);
          AddBiasVector<byRow>(T3_, w_.B3_);
      
          auto t = blaze::forEach(T1_ + T2_ + T3_, Tanh());
      
          if(!filtered_) {
            Probs_ = t * w_.W4_;
            AddBiasVector<byRow>(Probs_, w_.B4_);
          } else {
            Probs_ = t * FilteredW4_;
            AddBiasVector<byRow>(Probs_, FilteredB4_);
          }
          mblas::Softmax(Probs_);
          Probs = blaze::forEach(Probs_, Log());
        }
    
        void Filter(const std::vector<size_t>& ids) {
          filtered_ = true;
          using namespace mblas;
          FilteredW4_ = Assemble<byColumn, Matrix>(w_.W4_, ids);
          FilteredB4_ = Assemble<byColumn, Matrix>(w_.B4_, ids);
        }
       
      private:        
        const Weights& w_;
        bool filtered_;
        
        mblas::Matrix FilteredW4_;
        mblas::Matrix FilteredB4_;
        
        mblas::Matrix T1_;
        mblas::Matrix T2_;
        mblas::Matrix T3_;
        mblas::Matrix Probs_;
    };
    
  public:
    Decoder(const Weights& model);
    
    void MakeStep(mblas::Matrix& NextState,
                  mblas::ArrayMatrix& Probs,
                  const mblas::Matrix& State,
                  const mblas::Matrix& Embeddings,
                  const mblas::Matrix& SourceContext);
    
    void EmptyState(mblas::Matrix& State,
                    const mblas::Matrix& SourceContext,
                    size_t batchSize = 1);
    
    void EmptyEmbedding(mblas::Matrix& Embedding,
                        size_t batchSize = 1);
    
    void Lookup(mblas::Matrix& Embedding,
                const std::vector<size_t>& w);
    
    void Filter(const std::vector<size_t>& ids);
      
    void GetAttention(mblas::Matrix& attention);
    
    size_t GetVocabSize() const;
    
  private:
    
    void GetHiddenState(mblas::Matrix& HiddenState,
                        const mblas::Matrix& PrevState,
                        const mblas::Matrix& Embedding);
    
    void GetAlignedSourceContext(mblas::Matrix& AlignedSourceContext,
                                 const mblas::Matrix& HiddenState,
                                 const mblas::Matrix& SourceContext);
    
    void GetNextState(mblas::Matrix& State,
                      const mblas::Matrix& HiddenState,
                      const mblas::Matrix& AlignedSourceContext);
    
    void GetProbs(mblas::ArrayMatrix& Probs,
                  const mblas::Matrix& State,
                  const mblas::Matrix& Embedding,
                  const mblas::Matrix& AlignedSourceContext);
    
  private:
    mblas::Matrix HiddenState_;
    mblas::Matrix AlignedSourceContext_;  
    
    Embeddings<Weights::Embeddings> embeddings_;
    RNNHidden<Weights::DecInit, Weights::GRU> rnn1_;
    RNNFinal<Weights::DecGRU2> rnn2_;
    Attention<Weights::DecAttention> attention_;
    Softmax<Weights::DecSoftmax> softmax_;
};