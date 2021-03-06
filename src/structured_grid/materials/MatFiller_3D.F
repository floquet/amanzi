#undef  BL_LANG_CC
#ifndef BL_LANG_FORT
#define BL_LANG_FORT
#endif

#include <REAL.H>
#include <CONSTANTS.H>
#include <ArrayLim.H>
#include <MatFiller_F.H>

#define SDIM 3

      subroutine FORT_FILLP (p, DIMS(p),id,DIMS(id),lo,hi,vals)
      implicit none
      integer DIMDEC(p)
      integer DIMDEC(id)
      integer lo(SDIM), hi(SDIM)
      REAL_T   p(DIMV(p))
      integer id(DIMV(id))
      REAL_T vals(*)
      integer i,j,k
      do k = lo(3),hi(3)
         do j = lo(2),hi(2)
            do i = lo(1),hi(1)
               p(i,j,k) = vals(id(i,j,k)+1)
            end do
         end do
      end do
      end

      subroutine FORT_CRSNHARM (fdat, DIMS(fdat),
     &     cdat,DIMS(cdat),lo,hi,ratio,nc)
      implicit none
      integer DIMDEC(fdat)
      integer DIMDEC(cdat)
      integer lo(SDIM), hi(SDIM), nc
      integer ratio(SDIM)
      REAL_T  fdat(DIMV(fdat),nc)
      REAL_T  cdat(DIMV(cdat),nc)

      integer i,j,k,ii,jj,kk,fi,fj,fk
      REAL_T ktmp, fac1, fac2, fac3

      if (nc.lt.SDIM) then
         call bl_pd_abort('Not enough components for crsnharm')
      endif

      cdat(lo(1):hi(1),lo(2):hi(2),lo(3):hi(3),1:SDIM) = zero

      fac1 = DBLE(ratio(2)*ratio(3))
      fac2 = DBLE(ratio(1)*ratio(2))
      fac3 = DBLE(ratio(1)*ratio(3))

!$omp parallel private(i,j,k,fi,fj,fk,ii,jj,kk,ktmp)
!$omp do
c     ::: x-dir  Harmonic in x, arithmetic in y, z
      do kk = 0,ratio(3)-1
         do jj = 0,ratio(2)-1
            do k = lo(3),hi(3)
               fk = ratio(3)*k
               do j = lo(2),hi(2)
                  fj = ratio(2)*j
                  do i = lo(1),hi(1)
                     fi = ratio(1)*i
                     ktmp = zero
                     do ii = 0,ratio(1)-1
                        ktmp = ktmp + fac1/fdat(fi+ii,fj+jj,fk+kk,1)
                     enddo
                     cdat(i,j,k,1) = cdat(i,j,k,1) + ratio(1) / ktmp
                  end do
               end do
            end do
         end do
      end do
!$omp end do nowait

!$omp do
c     ::: y-dir  Harmonic in y, arithmetic in x, z
      do kk = 0,ratio(3)-1
         do ii = 0,ratio(1)-1
            do k = lo(3),hi(3)
               fk = ratio(3)*k
               do j = lo(2),hi(2)
                  fj = ratio(2)*j
                  do i = lo(1),hi(1)
                     fi = ratio(1)*i
                     ktmp = zero
                     do jj = 0,ratio(2)-1
                        ktmp = ktmp + fac2/fdat(fi+ii,fj+jj,fk+kk,2)
                     enddo
                     cdat(i,j,k,2) = cdat(i,j,k,2) + ratio(2) / ktmp
                  end do
               end do
            end do
         end do
      end do
!$omp end do nowait

!$omp do
c     ::: z-dir  Harmonic in z, arithmetic in x, y
      do jj = 0,ratio(2)-1
         do ii = 0,ratio(1)-1
            do k = lo(3),hi(3)
               fk = ratio(3)*k
               do j = lo(2),hi(2)
                  fj = ratio(2)*j
                  do i = lo(1),hi(1)
                     fi = ratio(1)*i
                     ktmp = zero
                     do kk = 0,ratio(3)-1
                        ktmp = ktmp + fac3/fdat(fi+ii,fj+jj,fk+kk,3)
                     enddo
                     cdat(i,j,k,3) = cdat(i,j,k,3) + ratio(3) / ktmp
                  end do
               end do
            end do
         end do
      end do
!$omp end do nowait
!$omp end parallel
      end


      subroutine FORT_CRSNARITH (fdat,DIMS(fdat),
     &     cdat,DIMS(cdat),lo,hi,ratio,nc)

      implicit none
      integer DIMDEC(cdat)
      integer DIMDEC(fdat)
      integer lo(SDIM), hi(SDIM), nc
      integer ratio(SDIM)
      REAL_T  cdat(DIMV(cdat),nc)
      REAL_T  fdat(DIMV(fdat),nc)

      integer i,j,k,ii,jj,kk,fi,fj,fk
      REAL_T fac

      cdat(lo(1):hi(1),lo(2):hi(2),lo(3):hi(3),1:nc) = zero
      fac = 1.d0 / dble(ratio(1)*ratio(2)*ratio(3))

!$omp parallel private(i,j,k,fi,fj,fk,ii,jj,kk,ktmp)
!$omp do
      do ii = 0,ratio(1)-1
         do jj = 0,ratio(2)-1
            do kk = 0,ratio(3)-1
               do k = lo(3),hi(3)
                  fk = ratio(3)*k
                  do j = lo(2),hi(2)
                     fj = ratio(2)*j
                     do i = lo(1),hi(1)        
                        fi = ratio(1)*i
                        cdat(i,j,k,1:nc) = cdat(i,j,k,1:nc) + fac*fdat(fi+ii,fj+jj,fk+jj,1:nc)
                     end do
                  end do
               end do
            end do
         end do
      end do
!$omp end do
!$omp end parallel

      end

      subroutine FORT_REFINEPC (cdat,DIMS(cdat),
     &     fdat,DIMS(fdat),lo,hi,ratio,nc)

      implicit none
      integer DIMDEC(fdat)
      integer DIMDEC(cdat)
      integer lo(SDIM), hi(SDIM), nc
      integer ratio(SDIM)
      REAL_T  fdat(DIMV(fdat),nc)
      REAL_T  cdat(DIMV(cdat),nc)

      integer i,j,k,ii,jj,kk,fi,fj,fk
      do kk = 0,ratio(3)-1
         do jj = 0,ratio(2)-1
            do ii = 0,ratio(1)-1
               do k = lo(3), hi(3)
                  fk = ratio(3)*k
                  do j = lo(2), hi(2)
                     fj = ratio(2)*j
                     do i = lo(1), hi(1)
                        fi = ratio(1)*i
                        fdat(fi+ii,fj+jj,fk+kk,1:nc) = cdat(i,j,k,1:nc)
                     end do
                  end do
               end do
            enddo
         end do
      end do
      end

