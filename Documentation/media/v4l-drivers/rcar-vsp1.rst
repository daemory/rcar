Renesas R-Car (VSP1) Driver
===========================

.. kernel-render:: DOT
   :alt: Display List Manager
   :caption: VSP1 Display List Manager

	digraph G {
		fontname = "Bitstream Vera Sans"
		fontsize = 8

		node [
			fontname = "Bitstream Vera Sans"
			fontsize = 8
			shape = "record"
		]

		edge [
			fontname = "Bitstream Vera Sans"
			fontsize = 8
		]

		DLM [
			label = "{DisplayListManager
				|
				unsigned int index; \l
				enum vsp1_dl_mode mode; \l
				struct vsp1_device *vsp1; \l
				\l
				spinlock_t lock; \l
				struct list_head free; \l
				struct vsp1_dl_list *active; \l
				struct vsp1_dl_list *queued; \l
				struct vsp1_dl_list *pending; \l
				\l
				struct work_struct gc_work; \l
				struct list_head gc_fragments; \l
				|struct vsp1_dl_manager \l
				   *vsp1_dlm_create(struct vsp1_device *vsp1, unsigned int index, unsigned int prealloc);\l
				void vsp1_dlm_destroy(struct vsp1_dl_manager *dlm);\l
				}"
		]

		subgraph Displaylist {
			label = "Displaylist"
	
			DLB [
			    label = "{dlb||+ bark() : void\l}"
			]
	
			Entries [
			    label = "{entries|reg : value \l reg : value \l...\l}"
			]

			DLB -> Entries
		}
	}
