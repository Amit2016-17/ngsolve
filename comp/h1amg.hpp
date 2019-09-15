#ifndef H1AMG_HPP_
#define H1AMG_HPP_

#include <comp.hpp>

namespace ngcomp
{
  template <class SCAL>
  class H1AMG_Matrix : public ngla::BaseMatrix
  {
    size_t size;
    std::shared_ptr<ngla::SparseMatrixTM<SCAL>> mat;
    std::shared_ptr<ngla::BaseBlockJacobiPrecond> smoother;
    std::shared_ptr<ngla::SparseMatrixTM<double>> prolongation, restriction;
    std::shared_ptr<ngla::BaseMatrix> coarse_precond;
    int smoothing_steps = 1;

  public:
    H1AMG_Matrix (std::shared_ptr<ngla::SparseMatrixTM<SCAL>> amat,
                  std::shared_ptr<ngcore::BitArray> freedofs,
                  ngcore::FlatArray<ngcore::INT<2>> e2v,
                  ngcore::FlatArray<double> edge_weights,
                  ngcore::FlatArray<double> vertex_weights,
                  size_t level);

    virtual int VHeight() const override { return size; }
    virtual int VWidth() const override { return size; }

    virtual AutoVector CreateRowVector () const override { return mat->CreateColVector(); }
    virtual AutoVector CreateColVector () const override { return mat->CreateRowVector(); }

    virtual void Mult (const ngla::BaseVector & b, ngla::BaseVector & x) const override;
  };
}

#endif // H1AMG_HPP_
