// 客房报价：按楼栋分组展示 24 档门市价
const api = require('../../utils/api')

const CUST = {
  government_enterprise: '政府/企业',
  study_travel: '研学/旅行社'
}
const BREAKFAST = { buffet: '自助早', table: '桌早' }

Page({
  data: {
    keyword: '',
    groups: [],
    total: 0,
    loading: false
  },

  onShow() {
    this.load()
  },

  onPullDownRefresh() {
    this.load().then(() => wx.stopPullDownRefresh())
  },

  load() {
    this.setData({ loading: true })
    return api
      .getRooms()
      .then((list) => {
        const kw = this.data.keyword.trim()
        const filtered = kw
          ? list.filter(
              (r) =>
                (r.building || '').indexOf(kw) >= 0 ||
                (r.room_type || '').indexOf(kw) >= 0 ||
                (r.category || '').indexOf(kw) >= 0
            )
          : list
        // 按楼栋分组
        const map = {}
        filtered.forEach((r) => {
          const b = r.building || '其他'
          if (!map[b]) map[b] = []
          map[b].push({
            ...r,
            cust_label: CUST[r.cust_type] || r.cust_type,
            breakfast_label: BREAKFAST[r.breakfast] || r.breakfast,
            price_text: '¥' + r.price
          })
        })
        const groups = Object.keys(map).map((name) => ({ name, items: map[name] }))
        this.setData({ groups, total: filtered.length, loading: false })
      })
      .catch((err) => {
        this.setData({ loading: false })
        wx.showToast({ title: '加载失败，请检查服务端', icon: 'none' })
        console.error(err)
      })
  },

  onSearch(e) {
    this.setData({ keyword: e.detail.value }, () => this.load())
  },

  onBook(e) {
    const item = e.currentTarget.dataset.item
    const title = item.building + item.room_type + (item.category === '净房' ? '（净房）' : '（含早）')
    wx.navigateTo({
      url:
        '/pages/booking/booking?type=room&item=' +
        encodeURIComponent(title) +
        '&amount=' +
        item.price
    })
  }
})
