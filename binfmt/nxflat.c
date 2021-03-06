/****************************************************************************
 * binfmt/nxflat.c
 *
 *   Copyright (C) 2009, 2019-2020 Gregory Nutt. All rights reserved.
 *   Author: Gregory Nutt <gnutt@nuttx.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name NuttX nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/types.h>
#include <stdint.h>
#include <string.h>
#include <nxflat.h>
#include <debug.h>
#include <errno.h>

#include <arpa/inet.h>

#include <nuttx/kmalloc.h>
#include <nuttx/binfmt/binfmt.h>
#include <nuttx/binfmt/nxflat.h>

#ifdef CONFIG_NXFLAT

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* CONFIG_DEBUG_FEATURES, CONFIG_DEBUG_INFO, and CONFIG_DEBUG_BINFMT have to be
 * defined or CONFIG_NXFLAT_DUMPBUFFER does nothing.
 */

#if !defined(CONFIG_DEBUG_INFO) || !defined (CONFIG_DEBUG_BINFMT)
#  undef CONFIG_NXFLAT_DUMPBUFFER
#endif

#ifdef CONFIG_NXFLAT_DUMPBUFFER
# define nxflat_dumpbuffer(m,b,n) binfodumpbuffer(m,b,n)
#else
# define nxflat_dumpbuffer(m,b,n)
#endif

#ifndef MIN
#  define MIN(a,b) (a < b ? a : b)
#endif

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int nxflat_loadbinary(FAR struct binary_s *binp);
static int nxflat_unloadbinary(FAR struct binary_s *binp);

#if defined(CONFIG_DEBUG_FEATURES) && defined(CONFIG_DEBUG_BINFMT)
static void nxflat_dumploadinfo(FAR struct nxflat_loadinfo_s *loadinfo);
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct binfmt_s g_nxflatbinfmt =
{
  NULL,                /* next */
  nxflat_loadbinary,   /* load */
  nxflat_unloadbinary, /* unload */
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: nxflat_dumploadinfo
 ****************************************************************************/

#if defined(CONFIG_DEBUG_FEATURES) && defined(CONFIG_DEBUG_BINFMT)
static void nxflat_dumploadinfo(FAR struct nxflat_loadinfo_s *loadinfo)
{
  unsigned long dsize = loadinfo->datasize + loadinfo->bsssize;

  binfo("LOAD_INFO:\n");
  binfo("  ISPACE:\n");
  binfo("    ispace:       %08lx\n", loadinfo->ispace);
  binfo("    entryoffs:    %08lx\n", loadinfo->entryoffs);
  binfo("    isize:        %08lx\n", loadinfo->isize);

  binfo("  DSPACE:\n");
  binfo("    dspace:       %08lx\n", loadinfo->dspace);

  if (loadinfo->dspace != NULL)
    {
      binfo("      crefs:      %d\n",    loadinfo->dspace->crefs);
      binfo("      region:     %08lx\n", loadinfo->dspace->region);
    }

  binfo("    datasize:     %08lx\n", loadinfo->datasize);
  binfo("    bsssize:      %08lx\n", loadinfo->bsssize);
  binfo("      (pad):      %08lx\n", loadinfo->dsize - dsize);
  binfo("    stacksize:    %08lx\n", loadinfo->stacksize);
  binfo("    dsize:        %08lx\n", loadinfo->dsize);

  binfo("  RELOCS:\n");
  binfo("    relocstart:   %08lx\n", loadinfo->relocstart);
  binfo("    reloccount:   %d\n",    loadinfo->reloccount);

  binfo("  HANDLES:\n");
  binfo("    filfd:        %d\n",    loadinfo->filfd);
}
#else
# define nxflat_dumploadinfo(i)
#endif

/****************************************************************************
 * Name: nxflat_loadbinary
 *
 * Description:
 *   Verify that the file is an NXFLAT binary and, if so, load the NXFLAT
 *   binary into memory
 *
 ****************************************************************************/

static int nxflat_loadbinary(FAR struct binary_s *binp)
{
  struct nxflat_loadinfo_s loadinfo;  /* Contains globals for libnxflat */
  int                      ret;

  binfo("Loading file: %s\n", binp->filename);

  /* Initialize the xflat library to load the program binary. */

  ret = nxflat_init(binp->filename, &loadinfo);
  nxflat_dumploadinfo(&loadinfo);
  if (ret != 0)
    {
      berr("Failed to initialize for load of NXFLAT program: %d\n", ret);
      goto errout;
    }

  /* Load the program binary */

  ret = nxflat_load(&loadinfo);
  nxflat_dumploadinfo(&loadinfo);
  if (ret != 0)
    {
      berr("Failed to load NXFLAT program binary: %d\n", ret);
      goto errout_with_init;
    }

  /* Bind the program to the exported symbol table */

  ret = nxflat_bind(&loadinfo, binp->exports, binp->nexports);
  if (ret != 0)
    {
      berr("Failed to bind symbols program binary: %d\n", ret);
      goto errout_with_load;
    }

  /* Return the load information.  By convention, D-space address
   * space is stored as the first allocated memory.
   */

  binp->entrypt   = (main_t)(loadinfo.ispace + loadinfo.entryoffs);
  binp->mapped    = (FAR void *)loadinfo.ispace;
  binp->mapsize   = loadinfo.isize;
  binp->stacksize = loadinfo.stacksize;

  /* Add the ELF allocation to the alloc[] only if there is no address
   * enironment.  If there is an address environment, it will automatically
   * be freed when the function exits
   *
   * REVISIT:  If the module is loaded then unloaded, wouldn't this cause
   * a memory leak?
   */

#ifdef CONFIG_ARCH_ADDRENV
#  warning "REVISIT"
#else
  binp->alloc[0]  = (FAR void *)loadinfo.dspace;
#endif

#ifdef CONFIG_ARCH_ADDRENV
  /* Save the address environment in the binfmt structure.  This will be
   * needed when the module is executed.
   */

  up_addrenv_clone(&loadinfo.addrenv, &binp->addrenv);
#endif

  nxflat_dumpbuffer("Entry code", (FAR const uint8_t *)binp->entrypt,
                    MIN(loadinfo.isize - loadinfo.entryoffs, 512));

  nxflat_uninit(&loadinfo);
  return OK;

errout_with_load:
  nxflat_unload(&loadinfo);
errout_with_init:
  nxflat_uninit(&loadinfo);
errout:
  return ret;
}

/****************************************************************************
 * Name: nxflat_unloadbinary
 *
 * Description:
 *   Verify that the file is an NXFLAT binary and, if so, load the NXFLAT
 *   binary into memory
 *
 ****************************************************************************/

static int nxflat_unloadbinary(FAR struct binary_s *binp)
{
  FAR struct dspace_s *dspace = (FAR struct dspace_s *)binp->alloc[0];

  /* Check if this is the last reference to dspace.  It may still be needed
   * by other threads.  In that case, it must persist after this thread
   * terminates.
   */

  if (dspace != NULL && dspace->crefs == 1)
    {
      /* Free the dspace region */

      kumm_free(dspace->region);
      dspace->region = NULL;

      /* Mark alloc[0] (dspace) as freed */

      binp->alloc[0] = NULL;

      /* The reference count will be decremented to zero and the dspace
       * container will be freed in sched/sched_releasetcb.c
       */
    }

  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: nxflat_initialize
 *
 * Description:
 *   In order to use the NxFLAT binary format, this function must be called
 *   during system initialization to register the NXFLAT binary
 *   format.
 *
 * Returned Value:
 *   This is a NuttX internal function so it follows the convention that
 *   0 (OK) is returned on success and a negated errno is returned on
 *   failure.
 *
 ****************************************************************************/

int nxflat_initialize(void)
{
  int ret;

  /* Register ourselves as a binfmt loader */

  binfo("Registering NXFLAT\n");
  ret = register_binfmt(&g_nxflatbinfmt);
  if (ret != 0)
    {
      berr("Failed to register binfmt: %d\n", ret);
    }

  return ret;
}

/****************************************************************************
 * Name: nxflat_uninitialize
 *
 * Description:
 *   Unregister the NXFLAT binary loader
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void nxflat_uninitialize(void)
{
  unregister_binfmt(&g_nxflatbinfmt);
}

#endif /* CONFIG_NXFLAT */
